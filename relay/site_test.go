// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

package main

import (
	"io"
	"net/http"
	"net/http/httptest"
	"os"
	"path/filepath"
	"testing"
)

func newTestSite(t *testing.T) *http.ServeMux {
	t.Helper()
	dir := t.TempDir()
	write := func(name, body string) {
		p := filepath.Join(dir, name)
		if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
			t.Fatal(err)
		}
		if err := os.WriteFile(p, []byte(body), 0o644); err != nil {
			t.Fatal(err)
		}
	}
	write("index.html", "home")
	write("blog/index.html", "blog")
	write("downloads/biscuit.dmg", "dmg")
	write(".git/config", "secret")

	stub := func(body string) http.Handler {
		return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, body) })
	}
	return routes("relay.example.org", stub("api"), stub("ok"), "example.org", staticSite(dir))
}

func get(mux *http.ServeMux, method, url string) *httptest.ResponseRecorder {
	rec := httptest.NewRecorder()
	mux.ServeHTTP(rec, httptest.NewRequest(method, url, nil))
	return rec
}

func TestSiteAndRelayAreSeparatedByHost(t *testing.T) {
	mux := newTestSite(t)
	cases := []struct {
		url  string
		code int
		body string
	}{
		{"https://example.org/", 200, "home"},
		{"https://example.org/blog/", 200, "blog"},
		{"https://example.org/downloads/biscuit.dmg", 200, "dmg"},
		{"https://relay.example.org/api/coins", 200, "api"},
		{"https://relay.example.org/health", 200, "ok"},
		{"https://example.org/api/coins", 404, ""},
		{"https://relay.example.org/", 404, ""},
		{"https://unknown.example.net/", 404, ""},
	}
	for _, c := range cases {
		rec := get(mux, http.MethodGet, c.url)
		if rec.Code != c.code || (c.body != "" && rec.Body.String() != c.body) {
			t.Errorf("%s: got %d %q, want %d %q", c.url, rec.Code, rec.Body.String(), c.code, c.body)
		}
	}
}

func TestWwwRedirectsToApex(t *testing.T) {
	rec := get(newTestSite(t), http.MethodGet, "https://www.example.org/blog/?a=1")
	if rec.Code != http.StatusMovedPermanently || rec.Header().Get("Location") != "https://example.org/blog/?a=1" {
		t.Fatalf("got %d %q", rec.Code, rec.Header().Get("Location"))
	}
}

func TestSiteHidesListingsAndDotfiles(t *testing.T) {
	mux := newTestSite(t)
	for _, url := range []string{"https://example.org/downloads/", "https://example.org/.git/config", "https://example.org/.git/"} {
		if rec := get(mux, http.MethodGet, url); rec.Code != 404 {
			t.Errorf("%s: got %d, want 404", url, rec.Code)
		}
	}
}

func TestSiteIsReadOnlyWithSecurityHeaders(t *testing.T) {
	mux := newTestSite(t)
	if rec := get(mux, http.MethodPost, "https://example.org/"); rec.Code != http.StatusMethodNotAllowed {
		t.Errorf("POST: got %d", rec.Code)
	}
	rec := get(mux, http.MethodGet, "https://example.org/")
	for _, h := range []string{"Content-Security-Policy", "X-Content-Type-Options", "Referrer-Policy", "Strict-Transport-Security"} {
		if rec.Header().Get(h) == "" {
			t.Errorf("missing %s", h)
		}
	}
}
