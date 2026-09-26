// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"filippo.io/age"
)

var testNow = time.Date(2026, 9, 26, 14, 30, 0, 0, time.UTC)

type fakeTrocador struct {
	srv      *httptest.Server
	calls    int
	lastReq  *http.Request
	status   int
	response string
}

func newFakeTrocador(t *testing.T) *fakeTrocador {
	f := &fakeTrocador{status: 200, response: `{"trade_id":"ABC123","address_provider":"deposit"}`}
	f.srv = httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		f.calls++
		f.lastReq = r.Clone(context.Background())
		w.Header().Set("Set-Cookie", "tracking=1")
		w.WriteHeader(f.status)
		fmt.Fprint(w, f.response)
	}))
	t.Cleanup(f.srv.Close)
	return f
}

func newTestRelay(t *testing.T, up *fakeTrocador) (*Relay, *age.X25519Identity) {
	id, err := age.GenerateX25519Identity()
	if err != nil {
		t.Fatal(err)
	}
	tor := &TorExits{ips: map[string]bool{"185.220.101.1": true}, loaded: true}
	return &Relay{
		Upstream: up.srv.URL + "/api/",
		APIKey:   "secret-key",
		Client:   up.srv.Client(),
		Trades:   &TradeLog{Dir: t.TempDir(), Recipient: id.Recipient(), Retention: 365 * 24 * time.Hour},
		Tor:      tor,
		Limiter:  &Limiter{PerMinute: 30, TradesPerWindow: 5, TradeWindow: 10 * time.Minute},
		Now:      func() time.Time { return testNow },
	}, id
}

func do(rl *Relay, method, target, remote string, headers map[string]string) *httptest.ResponseRecorder {
	req := httptest.NewRequest(method, target, nil)
	req.RemoteAddr = remote
	for k, v := range headers {
		req.Header.Set(k, v)
	}
	rec := httptest.NewRecorder()
	rl.ServeHTTP(rec, req)
	return rec
}

func logFiles(t *testing.T, dir string) []string {
	files, err := filepath.Glob(filepath.Join(dir, "*.log"))
	if err != nil {
		t.Fatal(err)
	}
	return files
}

func TestRefusesUnknownEndpointsAndMethods(t *testing.T) {
	up := newFakeTrocador(t)
	rl, _ := newTestRelay(t, up)
	for _, target := range []string{"/api/new_bridge", "/api/", "/api/coins/../admin", "/other"} {
		if rec := do(rl, "GET", target, "1.2.3.4:1000", nil); rec.Code != http.StatusNotFound {
			t.Errorf("%s: got %d", target, rec.Code)
		}
	}
	if rec := do(rl, "POST", "/api/coins", "1.2.3.4:1000", nil); rec.Code != http.StatusMethodNotAllowed {
		t.Errorf("POST: got %d", rec.Code)
	}
	if up.calls != 0 {
		t.Errorf("upstream called %d times", up.calls)
	}
}

func TestForwardsWithKeyAndNothingAboutTheUser(t *testing.T) {
	up := newFakeTrocador(t)
	up.response = `[{"ticker":"xmr"}]`
	rl, _ := newTestRelay(t, up)
	rec := do(rl, "GET", "/api/new_rate?ticker_from=xmr&amount_from=1.5", "1.2.3.4:1000", map[string]string{
		"API-Key":         "attacker-key",
		"Cookie":          "session=1",
		"X-Forwarded-For": "9.9.9.9",
		"User-Agent":      "Biscuit/1.0",
		"Accept-Language": "fr",
	})
	if rec.Code != 200 || rec.Body.String() != up.response {
		t.Fatalf("got %d %q", rec.Code, rec.Body.String())
	}
	h := up.lastReq.Header
	if h.Get("API-Key") != "secret-key" {
		t.Errorf("API-Key = %q", h.Get("API-Key"))
	}
	for _, name := range []string{"Cookie", "X-Forwarded-For", "Accept-Language"} {
		if h.Get(name) != "" {
			t.Errorf("%s leaked to Trocador: %q", name, h.Get(name))
		}
	}
	if h.Get("User-Agent") != "biscuit-relay" {
		t.Errorf("User-Agent = %q", h.Get("User-Agent"))
	}
	if q := up.lastReq.URL.Query(); q.Get("ticker_from") != "xmr" || q.Get("amount_from") != "1.5" {
		t.Errorf("query = %v", up.lastReq.URL.RawQuery)
	}
	if rec.Header().Get("Set-Cookie") != "" {
		t.Error("Trocador headers passed back to the app")
	}
	if files := logFiles(t, rl.Trades.Dir); len(files) != 0 {
		t.Errorf("non-trade request logged: %v", files)
	}
}

func TestNewTradeIsLoggedEncrypted(t *testing.T) {
	up := newFakeTrocador(t)
	rl, id := newTestRelay(t, up)
	rec := do(rl, "GET", "/api/new_trade?id=R1&address=4abc", "1.2.3.4:1000", map[string]string{
		"User-Agent":      "Biscuit/1.0 (macOS)",
		"Accept-Language": "fr-FR",
		"X-Forwarded-For": "9.9.9.9",
	})
	if rec.Code != 200 {
		t.Fatalf("got %d %s", rec.Code, rec.Body.String())
	}
	files := logFiles(t, rl.Trades.Dir)
	if len(files) != 1 || filepath.Base(files[0]) != "2026-09-26.log" {
		t.Fatalf("log files = %v", files)
	}
	raw, _ := os.ReadFile(files[0])
	for _, plain := range []string{"1.2.3.4", "ABC123", "Biscuit", "4abc"} {
		if bytes.Contains(raw, []byte(plain)) {
			t.Errorf("log file contains %q in clear", plain)
		}
	}
	if info, _ := os.Stat(files[0]); info.Mode().Perm() != 0o600 {
		t.Errorf("log file mode %v", info.Mode().Perm())
	}

	var out bytes.Buffer
	if err := Decrypt([]age.Identity{id}, files, &out); err != nil {
		t.Fatal(err)
	}
	var e TradeEntry
	if err := json.Unmarshal(out.Bytes(), &e); err != nil {
		t.Fatalf("%v: %s", err, out.String())
	}
	want := TradeEntry{Time: "2026-09-26T14:30:00Z", IP: "1.2.3.4", UserAgent: "Biscuit/1.0 (macOS)", Language: "fr-FR", TradeID: "ABC123"}
	if e != want {
		t.Errorf("entry = %+v, want %+v", e, want)
	}

	other, _ := age.GenerateX25519Identity()
	if err := Decrypt([]age.Identity{other}, files, &bytes.Buffer{}); err == nil {
		t.Error("decrypted with the wrong key")
	}
}

func TestFailedTradeIsNotLogged(t *testing.T) {
	up := newFakeTrocador(t)
	up.status, up.response = 400, `{"error":"bad address"}`
	rl, _ := newTestRelay(t, up)
	rec := do(rl, "GET", "/api/new_trade?id=R1", "1.2.3.4:1000", nil)
	if rec.Code != 400 || !strings.Contains(rec.Body.String(), "bad address") {
		t.Fatalf("got %d %s", rec.Code, rec.Body.String())
	}
	if files := logFiles(t, rl.Trades.Dir); len(files) != 0 {
		t.Errorf("failed trade logged: %v", files)
	}
}

func TestTradeRefusedFromTorExit(t *testing.T) {
	up := newFakeTrocador(t)
	rl, _ := newTestRelay(t, up)
	rec := do(rl, "GET", "/api/new_trade?id=R1", "185.220.101.1:1000", nil)
	if rec.Code != http.StatusForbidden || !strings.Contains(rec.Body.String(), "tor_exit") {
		t.Fatalf("got %d %s", rec.Code, rec.Body.String())
	}
	// Rates and tracking stay available, so a running trade can be followed.
	if rec := do(rl, "GET", "/api/trade?id=ABC123", "185.220.101.1:1000", nil); rec.Code != 200 {
		t.Errorf("trade status from Tor: got %d", rec.Code)
	}
	if up.calls != 1 {
		t.Errorf("upstream calls = %d, want 1", up.calls)
	}
}

func TestTradeRefusedUntilTorListLoaded(t *testing.T) {
	up := newFakeTrocador(t)
	rl, _ := newTestRelay(t, up)
	rl.Tor = &TorExits{}
	if rec := do(rl, "GET", "/api/new_trade?id=R1", "1.2.3.4:1000", nil); rec.Code != http.StatusServiceUnavailable {
		t.Fatalf("got %d", rec.Code)
	}
	if up.calls != 0 {
		t.Error("trade created without a Tor list")
	}
}

func TestDepositAddressWithheldWhenLogFails(t *testing.T) {
	up := newFakeTrocador(t)
	rl, _ := newTestRelay(t, up)
	rl.Trades.Dir = filepath.Join(t.TempDir(), "missing")
	rec := do(rl, "GET", "/api/new_trade?id=R1", "1.2.3.4:1000", nil)
	if rec.Code != http.StatusServiceUnavailable || strings.Contains(rec.Body.String(), "deposit") {
		t.Fatalf("got %d %s", rec.Code, rec.Body.String())
	}
}

func TestRateLimits(t *testing.T) {
	l := &Limiter{PerMinute: 3, TradesPerWindow: 1, TradeWindow: 10 * time.Minute}
	for i := 0; i < 3; i++ {
		if !l.Allow("a", false, testNow) {
			t.Fatalf("request %d refused", i)
		}
	}
	if l.Allow("a", false, testNow) {
		t.Error("4th request allowed")
	}
	if !l.Allow("b", false, testNow) {
		t.Error("other IP refused")
	}
	if !l.Allow("a", true, testNow.Add(time.Minute)) {
		t.Error("first trade refused after the minute")
	}
	if l.Allow("a", true, testNow.Add(2*time.Minute)) {
		t.Error("second trade within the window allowed")
	}
	l.Sweep(testNow.Add(time.Hour))
	if len(l.requests)+len(l.trades) != 0 {
		t.Error("sweep kept IPs in memory")
	}
}

func TestPurgeKeepsTwelveMonths(t *testing.T) {
	dir := t.TempDir()
	for _, name := range []string{"2025-09-25.log", "2025-09-26.log", "2026-09-26.log", "notes.txt"} {
		os.WriteFile(filepath.Join(dir, name), nil, 0o600)
	}
	tl := &TradeLog{Dir: dir, Retention: 365 * 24 * time.Hour}
	removed, err := tl.Purge(testNow)
	if err != nil || removed != 1 {
		t.Fatalf("removed %d, err %v", removed, err)
	}
	if _, err := os.Stat(filepath.Join(dir, "2025-09-25.log")); !os.IsNotExist(err) {
		t.Error("old day kept")
	}
	for _, name := range []string{"2025-09-26.log", "2026-09-26.log", "notes.txt"} {
		if _, err := os.Stat(filepath.Join(dir, name)); err != nil {
			t.Errorf("%s removed", name)
		}
	}
}

func TestTorExitListRefresh(t *testing.T) {
	var body strings.Builder
	body.WriteString("# header\n")
	for i := 0; i < 150; i++ {
		fmt.Fprintf(&body, "10.0.%d.%d\n", i/250, i%250)
	}
	body.WriteString("2001:db8::1\n")
	list := body.String()
	srv := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { fmt.Fprint(w, list) }))
	defer srv.Close()

	tor := &TorExits{URL: srv.URL, Client: srv.Client()}
	if err := tor.Refresh(context.Background()); err != nil {
		t.Fatal(err)
	}
	if tor.Contains("10.0.0.5") != torExit || tor.Contains("::ffff:10.0.0.5") != torExit || tor.Contains("2001:db8::1") != torExit {
		t.Error("exit not found")
	}
	if tor.Contains("1.2.3.4") != notTorExit {
		t.Error("non-exit flagged")
	}

	list = "1.1.1.1\n"
	if err := tor.Refresh(context.Background()); err == nil {
		t.Error("truncated list accepted")
	}
	if tor.Contains("10.0.0.5") != torExit {
		t.Error("good list replaced by a bad one")
	}
}
