// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

package main

import (
	"encoding/json"
	"io"
	"log"
	"net"
	"net/http"
	"net/url"
	"strings"
	"time"
)

// Trocador methods the app uses. Everything else is refused.
var allowedMethods = map[string]bool{
	"coins":           true,
	"new_rate":        true,
	"new_trade":       true,
	"trade":           true,
	"validateaddress": true,
}

const (
	maxQueryBytes    = 4 << 10
	maxResponseBytes = 8 << 20
	maxUserAgent     = 256
	maxLanguage      = 64
)

// Relay forwards whitelisted requests to Trocador with the partner key and
// records the connection data of each created trade. Nothing else is logged.
type Relay struct {
	Upstream string // e.g. "https://trocador.app/api/"
	APIKey   string
	Client   *http.Client
	Trades   *TradeLog
	Tor      *TorExits
	Limiter  *Limiter
	Now      func() time.Time
}

func (rl *Relay) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	method, ok := strings.CutPrefix(r.URL.Path, "/api/")
	if !ok || !allowedMethods[method] {
		writeError(w, http.StatusNotFound, "not_found", "Unknown endpoint.")
		return
	}
	if r.Method != http.MethodGet {
		writeError(w, http.StatusMethodNotAllowed, "method_not_allowed", "Only GET is allowed.")
		return
	}
	if len(r.URL.RawQuery) > maxQueryBytes {
		writeError(w, http.StatusRequestURITooLong, "query_too_long", "Query too long.")
		return
	}

	ip := clientIP(r)
	if !rl.Limiter.Allow(ip, method == "new_trade", rl.Now()) {
		writeError(w, http.StatusTooManyRequests, "rate_limited", "Too many requests. Try again in a few minutes.")
		return
	}

	if method == "new_trade" {
		switch rl.Tor.Contains(ip) {
		case torListMissing:
			writeError(w, http.StatusServiceUnavailable, "unavailable", "Swaps are temporarily unavailable. Try again in a few minutes.")
			return
		case torExit:
			writeError(w, http.StatusForbidden, "tor_exit", "Exchange swaps cannot be created through Tor. Disable Tor or use an atomic swap.")
			return
		}
	}

	query, err := url.ParseQuery(r.URL.RawQuery)
	if err != nil {
		writeError(w, http.StatusBadRequest, "bad_query", "Malformed query.")
		return
	}
	upstream := rl.Upstream + method + "?" + query.Encode()
	req, err := http.NewRequestWithContext(r.Context(), http.MethodGet, upstream, nil)
	if err != nil {
		writeError(w, http.StatusInternalServerError, "internal", "Internal error.")
		return
	}
	// Only our own headers reach Trocador: the key, and nothing about the user.
	req.Header.Set("API-Key", rl.APIKey)
	req.Header.Set("Accept", "application/json")
	req.Header.Set("User-Agent", "biscuit-relay")

	resp, err := rl.Client.Do(req)
	if err != nil {
		writeError(w, http.StatusBadGateway, "upstream_unreachable", "Trocador is unreachable. Try again later.")
		return
	}
	defer resp.Body.Close()
	body, err := io.ReadAll(io.LimitReader(resp.Body, maxResponseBytes+1))
	if err != nil || len(body) > maxResponseBytes {
		writeError(w, http.StatusBadGateway, "upstream_error", "Invalid answer from Trocador.")
		return
	}

	if method == "new_trade" && resp.StatusCode == http.StatusOK {
		if tradeID := extractTradeID(body); tradeID != "" {
			entry := TradeEntry{
				Time:      rl.Now().UTC().Format(time.RFC3339),
				IP:        ip,
				UserAgent: truncate(r.Header.Get("User-Agent"), maxUserAgent),
				Language:  truncate(r.Header.Get("Accept-Language"), maxLanguage),
				TradeID:   tradeID,
			}
			if err := rl.Trades.Append(entry); err != nil {
				// The trade exists at Trocador, but without its record we must
				// not hand out the deposit address. Never log the entry itself.
				log.Printf("trade log write failed: %v", err)
				writeError(w, http.StatusServiceUnavailable, "unavailable", "Swaps are temporarily unavailable. Try again in a few minutes.")
				return
			}
		}
	}

	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(resp.StatusCode)
	w.Write(body)
}

func extractTradeID(body []byte) string {
	var obj struct {
		TradeID string `json:"trade_id"`
	}
	if json.Unmarshal(body, &obj) != nil {
		return ""
	}
	return obj.TradeID
}

// clientIP is the TCP peer: nothing sits in front of the relay, so forwarding
// headers are never trusted.
func clientIP(r *http.Request) string {
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func truncate(s string, n int) string {
	if len(s) > n {
		return s[:n]
	}
	return s
}

func writeError(w http.ResponseWriter, status int, code, message string) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	json.NewEncoder(w).Encode(map[string]string{"error": code, "message": message})
}
