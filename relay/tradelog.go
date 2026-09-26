// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

package main

import (
	"bufio"
	"bytes"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"
	"time"

	"filippo.io/age"
)

// TradeEntry is the connection data Trocador requires for each created trade.
type TradeEntry struct {
	Time      string `json:"time"`
	IP        string `json:"ip"`
	UserAgent string `json:"user_agent"`
	Language  string `json:"language"`
	TradeID   string `json:"trade_id"`
}

// TradeLog appends one age-encrypted, base64 line per trade to a file per day
// (YYYY-MM-DD.log). The server only holds the public key: once written, an
// entry can only be read with the private key kept offline.
type TradeLog struct {
	Dir       string
	Recipient age.Recipient
	Retention time.Duration

	mu sync.Mutex
}

const dayLayout = "2006-01-02"

func (t *TradeLog) Append(e TradeEntry) error {
	plain, err := json.Marshal(e)
	if err != nil {
		return err
	}
	var enc bytes.Buffer
	wc, err := age.Encrypt(&enc, t.Recipient)
	if err != nil {
		return err
	}
	if _, err := wc.Write(plain); err != nil {
		return err
	}
	if err := wc.Close(); err != nil {
		return err
	}
	line := base64.StdEncoding.EncodeToString(enc.Bytes()) + "\n"

	day, err := time.Parse(time.RFC3339, e.Time)
	if err != nil {
		return err
	}
	path := filepath.Join(t.Dir, day.UTC().Format(dayLayout)+".log")

	t.mu.Lock()
	defer t.mu.Unlock()
	f, err := os.OpenFile(path, os.O_WRONLY|os.O_CREATE|os.O_APPEND, 0o600)
	if err != nil {
		return err
	}
	if _, err := f.WriteString(line); err != nil {
		f.Close()
		return err
	}
	if err := f.Sync(); err != nil {
		f.Close()
		return err
	}
	return f.Close()
}

// Purge deletes the day files whose whole day is older than the retention.
func (t *TradeLog) Purge(now time.Time) (int, error) {
	names, err := os.ReadDir(t.Dir)
	if err != nil {
		return 0, err
	}
	cutoff := now.UTC().Add(-t.Retention)
	removed := 0
	for _, n := range names {
		day, ok := strings.CutSuffix(n.Name(), ".log")
		if !ok || n.IsDir() {
			continue
		}
		d, err := time.Parse(dayLayout, day)
		if err != nil {
			continue
		}
		if d.AddDate(0, 0, 1).Before(cutoff) {
			if err := os.Remove(filepath.Join(t.Dir, n.Name())); err != nil {
				return removed, err
			}
			removed++
		}
	}
	return removed, nil
}

// Decrypt prints the entries of the given files as JSON lines, oldest file
// first. Used offline, on the machine holding the private key.
func Decrypt(identities []age.Identity, files []string, out io.Writer) error {
	sort.Strings(files)
	for _, path := range files {
		f, err := os.Open(path)
		if err != nil {
			return err
		}
		sc := bufio.NewScanner(f)
		sc.Buffer(make([]byte, 64<<10), 64<<10)
		lineNo := 0
		for sc.Scan() {
			lineNo++
			raw, err := base64.StdEncoding.DecodeString(strings.TrimSpace(sc.Text()))
			if err != nil {
				f.Close()
				return fmt.Errorf("%s:%d: %v", path, lineNo, err)
			}
			r, err := age.Decrypt(bytes.NewReader(raw), identities...)
			if err != nil {
				f.Close()
				return fmt.Errorf("%s:%d: %v", path, lineNo, err)
			}
			plain, err := io.ReadAll(r)
			if err != nil {
				f.Close()
				return fmt.Errorf("%s:%d: %v", path, lineNo, err)
			}
			fmt.Fprintf(out, "%s\n", plain)
		}
		err = sc.Err()
		f.Close()
		if err != nil {
			return err
		}
	}
	return nil
}
