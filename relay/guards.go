// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

package main

import (
	"bufio"
	"context"
	"fmt"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

// TorBulkExitList is the Tor Project's list of exit IPs, measured from the
// exits themselves.
const TorBulkExitList = "https://check.torproject.org/torbulkexitlist"

type torStatus int

const (
	torListMissing torStatus = iota // never loaded: trade creation fails closed
	torExit
	notTorExit
)

// TorExits holds the latest Tor exit list. A failed refresh keeps the previous
// list; before the first successful load, trade creation is refused.
type TorExits struct {
	URL    string
	Client *http.Client

	mu     sync.RWMutex
	ips    map[string]bool
	loaded bool
}

func (t *TorExits) Contains(ip string) torStatus {
	t.mu.RLock()
	defer t.mu.RUnlock()
	if !t.loaded {
		return torListMissing
	}
	if t.ips[normalizeIP(ip)] {
		return torExit
	}
	return notTorExit
}

func (t *TorExits) Refresh(ctx context.Context) error {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, t.URL, nil)
	if err != nil {
		return err
	}
	resp, err := t.Client.Do(req)
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("tor exit list: HTTP %d", resp.StatusCode)
	}
	ips := map[string]bool{}
	sc := bufio.NewScanner(io.LimitReader(resp.Body, 16<<20))
	for sc.Scan() {
		line := strings.TrimSpace(sc.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		if ip := normalizeIP(line); ip != "" {
			ips[ip] = true
		}
	}
	if err := sc.Err(); err != nil {
		return err
	}
	if len(ips) < 100 {
		// A truncated or bogus answer must not replace a good list.
		return fmt.Errorf("tor exit list: only %d addresses", len(ips))
	}
	t.mu.Lock()
	t.ips, t.loaded = ips, true
	t.mu.Unlock()
	return nil
}

func normalizeIP(s string) string {
	ip := net.ParseIP(s)
	if ip == nil {
		return ""
	}
	return ip.String()
}

// Limiter counts requests per IP in fixed one-minute windows, with a tighter
// separate budget for trade creation. Counters live in memory only and are
// dropped after their window ends.
type Limiter struct {
	PerMinute       int
	TradesPerWindow int
	TradeWindow     time.Duration

	mu       sync.Mutex
	requests map[string]*window
	trades   map[string]*window
}

type window struct {
	start time.Time
	count int
}

func (l *Limiter) Allow(ip string, isTrade bool, now time.Time) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	if l.requests == nil {
		l.requests, l.trades = map[string]*window{}, map[string]*window{}
	}
	if !hit(l.requests, ip, now, time.Minute, l.PerMinute) {
		return false
	}
	if isTrade && !hit(l.trades, ip, now, l.TradeWindow, l.TradesPerWindow) {
		return false
	}
	return true
}

func hit(m map[string]*window, ip string, now time.Time, length time.Duration, max int) bool {
	w := m[ip]
	if w == nil || now.Sub(w.start) >= length {
		w = &window{start: now}
		m[ip] = w
	}
	w.count++
	return w.count <= max
}

// Sweep forgets windows that have ended, so no IP stays in memory.
func (l *Limiter) Sweep(now time.Time) {
	l.mu.Lock()
	defer l.mu.Unlock()
	for ip, w := range l.requests {
		if now.Sub(w.start) >= time.Minute {
			delete(l.requests, ip)
		}
	}
	for ip, w := range l.trades {
		if now.Sub(w.start) >= l.TradeWindow {
			delete(l.trades, ip)
		}
	}
}
