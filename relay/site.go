// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

package main

import (
	"errors"
	"io/fs"
	"net/http"
	"os"
	"path"
	"strings"
)

// routes serves the relay API on relayHost and, when siteHost is set, the
// static website on siteHost with www.siteHost redirected to it. Anything else
// (unknown host, relay paths on the site, site paths on the relay) is a 404.
// Serving both from one program keeps the user's real IP reaching the relay
// directly, with no reverse proxy in between.
func routes(relayHost string, relay, health http.Handler, siteHost string, site http.Handler) *http.ServeMux {
	mux := http.NewServeMux()
	mux.Handle(relayHost+"/api/", relay)
	mux.Handle(relayHost+"/health", health)
	if siteHost != "" {
		mux.Handle(siteHost+"/", site)
		mux.HandleFunc("www."+siteHost+"/", func(w http.ResponseWriter, r *http.Request) {
			http.Redirect(w, r, "https://"+siteHost+r.URL.RequestURI(), http.StatusMovedPermanently)
		})
	}
	return mux
}

// staticSite serves the files in dir: no directory listings, no dotfiles,
// GET and HEAD only, and no logging of any kind.
func staticSite(dir string) http.Handler {
	files := http.FileServer(noListing{http.Dir(dir)})
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			w.Header().Set("Allow", "GET, HEAD")
			http.Error(w, "method not allowed", http.StatusMethodNotAllowed)
			return
		}
		h := w.Header()
		h.Set("X-Content-Type-Options", "nosniff")
		h.Set("Referrer-Policy", "no-referrer")
		h.Set("Content-Security-Policy", "default-src 'self'; style-src 'self' 'unsafe-inline'; img-src 'self' data:; frame-ancestors 'none'; base-uri 'none'; form-action 'none'")
		if r.TLS != nil {
			h.Set("Strict-Transport-Security", "max-age=31536000")
		}
		files.ServeHTTP(w, r)
	})
}

// noListing hides dotfiles and directories that have no index.html.
type noListing struct{ fs http.FileSystem }

func (n noListing) Open(name string) (http.File, error) {
	for _, part := range strings.Split(name, "/") {
		if strings.HasPrefix(part, ".") {
			return nil, fs.ErrNotExist
		}
	}
	f, err := n.fs.Open(name)
	if err != nil {
		return nil, err
	}
	st, err := f.Stat()
	if err != nil {
		f.Close()
		return nil, err
	}
	if st.IsDir() {
		index, err := n.fs.Open(path.Join(name, "index.html"))
		if err != nil {
			f.Close()
			if errors.Is(err, os.ErrNotExist) {
				return nil, fs.ErrNotExist
			}
			return nil, err
		}
		index.Close()
	}
	return f, nil
}
