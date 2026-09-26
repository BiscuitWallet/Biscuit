// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// biscuit-relay keeps Biscuit's Trocador partner key on the server and records
// the connection data Trocador's providers require for each created trade.
//
//	biscuit-relay serve   -domain relay.example.org -key-file ... -recipient age1...
//	biscuit-relay keygen  -out trades-identity.txt
//	biscuit-relay decrypt -identity trades-identity.txt logs/*.log
package main

import (
	"context"
	"crypto/tls"
	"flag"
	"fmt"
	"io"
	"log"
	"net/http"
	"os"
	"strings"
	"time"

	"filippo.io/age"
	"golang.org/x/crypto/acme/autocert"
)

func main() {
	log.SetFlags(0)
	if len(os.Args) < 2 {
		usage()
	}
	switch os.Args[1] {
	case "serve":
		serve(os.Args[2:])
	case "keygen":
		keygen(os.Args[2:])
	case "decrypt":
		decrypt(os.Args[2:])
	default:
		usage()
	}
}

func usage() {
	fmt.Fprintln(os.Stderr, "usage: biscuit-relay serve|keygen|decrypt [flags]")
	os.Exit(2)
}

func serve(args []string) {
	fs := flag.NewFlagSet("serve", flag.ExitOnError)
	domain := fs.String("domain", "", "public domain name (HTTPS with Let's Encrypt)")
	dev := fs.String("dev", "", "plain HTTP on this address instead of HTTPS, for local tests (e.g. 127.0.0.1:8080)")
	keyFile := fs.String("key-file", "", "file holding the Trocador API key")
	recipient := fs.String("recipient", "", "age public key (age1...) the trade log is encrypted to")
	logDir := fs.String("log-dir", "/var/lib/biscuit-relay/trades", "encrypted trade log directory")
	certDir := fs.String("cert-dir", "/var/lib/biscuit-relay/certs", "Let's Encrypt certificate cache")
	upstream := fs.String("upstream", "https://trocador.app/api/", "Trocador API base URL")
	retentionDays := fs.Int("retention-days", 365, "days a trade record is kept")
	fs.Parse(args)

	if (*domain == "") == (*dev == "") {
		log.Fatal("give exactly one of -domain or -dev")
	}
	keyBytes, err := os.ReadFile(*keyFile)
	if err != nil {
		log.Fatalf("reading key file: %v", err)
	}
	apiKey := strings.TrimSpace(string(keyBytes))
	if apiKey == "" {
		log.Fatal("key file is empty")
	}
	rcpt, err := age.ParseX25519Recipient(*recipient)
	if err != nil {
		log.Fatalf("recipient: %v", err)
	}
	if err := os.MkdirAll(*logDir, 0o700); err != nil {
		log.Fatal(err)
	}

	client := &http.Client{Timeout: 30 * time.Second}
	trades := &TradeLog{Dir: *logDir, Recipient: rcpt, Retention: time.Duration(*retentionDays) * 24 * time.Hour}
	tor := &TorExits{URL: TorBulkExitList, Client: client}
	// Generous: many users can share one VPN server IP (the app itself sends at
	// most 6 requests per minute). Only abuse of the relay is stopped.
	limiter := &Limiter{PerMinute: 120, TradesPerWindow: 20, TradeWindow: 10 * time.Minute}
	relay := &Relay{Upstream: *upstream, APIKey: apiKey, Client: client, Trades: trades, Tor: tor, Limiter: limiter, Now: time.Now}

	go refreshTorExits(tor)
	go every(time.Hour, func() {
		if _, err := trades.Purge(time.Now()); err != nil {
			log.Printf("purge: %v", err)
		}
	})
	go every(time.Minute, func() { limiter.Sweep(time.Now()) })

	mux := http.NewServeMux()
	mux.Handle("/api/", relay)
	mux.HandleFunc("/health", func(w http.ResponseWriter, r *http.Request) { io.WriteString(w, "ok\n") })

	// Go's server logs TLS and connection errors with the client's IP: discard
	// them, the relay keeps no IP outside the encrypted trade log.
	quiet := log.New(io.Discard, "", 0)
	newServer := func(addr string, h http.Handler) *http.Server {
		return &http.Server{
			Addr:              addr,
			Handler:           h,
			ErrorLog:          quiet,
			ReadHeaderTimeout: 10 * time.Second,
			ReadTimeout:       20 * time.Second,
			WriteTimeout:      60 * time.Second,
			IdleTimeout:       60 * time.Second,
			MaxHeaderBytes:    16 << 10,
		}
	}

	if *dev != "" {
		log.Printf("dev mode: http://%s", *dev)
		log.Fatal(newServer(*dev, mux).ListenAndServe())
	}

	if err := os.MkdirAll(*certDir, 0o700); err != nil {
		log.Fatal(err)
	}
	m := &autocert.Manager{
		Prompt:     autocert.AcceptTOS,
		HostPolicy: autocert.HostWhitelist(*domain),
		Cache:      autocert.DirCache(*certDir),
	}
	go func() {
		// Port 80: Let's Encrypt challenges, everything else redirected to HTTPS.
		log.Fatal(newServer(":80", m.HTTPHandler(nil)).ListenAndServe())
	}()
	srv := newServer(":443", mux)
	srv.TLSConfig = m.TLSConfig()
	srv.TLSConfig.MinVersion = tls.VersionTLS12
	log.Printf("serving https://%s", *domain)
	log.Fatal(srv.ListenAndServeTLS("", ""))
}

// refreshTorExits retries every minute until the first load, then refreshes
// every 30 minutes.
func refreshTorExits(tor *TorExits) {
	loaded := false
	for {
		ctx, cancel := context.WithTimeout(context.Background(), time.Minute)
		err := tor.Refresh(ctx)
		cancel()
		if err != nil {
			log.Printf("tor exit list: %v", err)
		} else {
			loaded = true
		}
		if loaded {
			time.Sleep(30 * time.Minute)
		} else {
			time.Sleep(time.Minute)
		}
	}
}

func every(d time.Duration, f func()) {
	for {
		f()
		time.Sleep(d)
	}
}

func keygen(args []string) {
	fs := flag.NewFlagSet("keygen", flag.ExitOnError)
	out := fs.String("out", "trades-identity.txt", "where to write the private key")
	fs.Parse(args)

	id, err := age.GenerateX25519Identity()
	if err != nil {
		log.Fatal(err)
	}
	f, err := os.OpenFile(*out, os.O_WRONLY|os.O_CREATE|os.O_EXCL, 0o600)
	if err != nil {
		log.Fatal(err)
	}
	fmt.Fprintf(f, "# Biscuit relay trade log private key. Keep offline.\n# public key: %s\n%s\n", id.Recipient(), id)
	if err := f.Close(); err != nil {
		log.Fatal(err)
	}
	fmt.Printf("private key written to %s (keep it offline)\npublic key for -recipient: %s\n", *out, id.Recipient())
}

func decrypt(args []string) {
	fs := flag.NewFlagSet("decrypt", flag.ExitOnError)
	identity := fs.String("identity", "", "private key file from keygen")
	fs.Parse(args)

	f, err := os.Open(*identity)
	if err != nil {
		log.Fatal(err)
	}
	ids, err := age.ParseIdentities(f)
	f.Close()
	if err != nil {
		log.Fatal(err)
	}
	if fs.NArg() == 0 {
		log.Fatal("give the .log files to decrypt")
	}
	if err := Decrypt(ids, fs.Args(), os.Stdout); err != nil {
		log.Fatal(err)
	}
}
