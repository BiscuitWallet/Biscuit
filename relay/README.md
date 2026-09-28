# Relais Trocador de Biscuit

Petit serveur qui garde la clé partenaire Trocador hors de l'application. Biscuit
appelle le relais, le relais ajoute la clé et transmet la demande à Trocador.

## Ce qu'il fait, et rien d'autre

- **Liste blanche** : seuls `coins`, `new_rate`, `new_trade`, `trade` et
  `validateaddress` passent, en GET. Tout le reste reçoit une 404.
- **Rien sur l'utilisateur vers Trocador** : Trocador ne reçoit que la clé et un
  user agent fixe (`biscuit-relay`). Ni IP, ni cookies, ni en-têtes de l'app.
- **Journal des échanges** : à chaque `new_trade` réussi, et seulement là, une ligne
  est écrite : date, IP, user agent, langue, ID de l'échange. C'est ce qu'exigent
  Trocador et ses fournisseurs pour les demandes des autorités.
  - Chaque ligne est **chiffrée avec une clé publique** ([age](https://age-encryption.org)).
    Le serveur ne peut pas la relire : seule la clé privée, gardée hors ligne, le peut.
  - Un fichier par jour (`AAAA-MM-JJ.log`), supprimé automatiquement après
    **12 mois** (vérification toutes les heures).
  - Si l'écriture échoue, l'app ne reçoit pas l'adresse de dépôt : pas d'échange
    sans trace.
- **Pas d'échange via Tor** : la création d'échange est refusée (403, `tor_exit`)
  depuis les sorties Tor, d'après la liste officielle du Tor Project, rechargée
  toutes les 30 minutes. Tant que la liste n'a jamais pu être chargée, la création
  d'échange est refusée (503). Taux et suivi restent possibles.
- **VPN acceptés** : seules les sorties Tor sont refusées. Beaucoup d'utilisateurs
  peuvent partager l'IP d'un même serveur VPN, d'où des limites larges : 120 requêtes
  par minute et 20 créations d'échange par 10 minutes par IP (l'app elle-même ne
  dépasse pas 6 requêtes par minute). Compteurs en mémoire uniquement, oubliés à la
  fin de chaque fenêtre.
- **Aucun autre journal** : les erreurs de connexion de Go (qui contiennent l'IP) sont
  jetées ; le programme n'écrit que ses propres erreurs, sans IP ni requête.
- **HTTPS intégré** : certificat Let's Encrypt obtenu et renouvelé tout seul. Pas de
  Caddy ni de nginx.
- `GET /health` répond `ok`, pour une surveillance externe.

## Tester sur le Mac

```sh
brew install go
cd relay
go test ./...
go build -o biscuit-relay .
./biscuit-relay keygen -out /tmp/test-identity.txt      # affiche la clé publique age1...
./biscuit-relay serve -dev 127.0.0.1:8080 -key-file <fichier avec la clé Trocador> \
    -recipient age1... -log-dir /tmp/relay-trades
curl 'http://127.0.0.1:8080/api/coins'
```

## Mise en service (Debian 12 ou 13, environ 30 minutes)

### 1. Clés (sur le Mac)

```sh
./biscuit-relay keygen -out trades-identity.txt
```

- `trades-identity.txt` est la **clé privée** du journal. Elle ne va **jamais** sur le
  serveur. La garder dans le gestionnaire de mots de passe et sur une clé USB.
  Sans elle, le journal est illisible, y compris pour répondre à une demande légale.
- La ligne `age1...` affichée est la clé publique, à mettre dans le service (étape 4).

### 2. Serveur

1. Louer le plus petit VPS Debian (1 vCPU, 1 Go de RAM suffit) chez un hébergeur de
   l'UE ou de l'EEE. **Désactiver les sauvegardes et instantanés** de l'hébergeur :
   ils copieraient le journal hors de la rotation de 12 mois.
2. Pointer le domaine vers le serveur : enregistrement `A` (et `AAAA` si IPv6).
3. En SSH (connexion par clé) :

```sh
apt update && apt full-upgrade -y
apt install -y unattended-upgrades nftables
dpkg-reconfigure -plow unattended-upgrades          # répondre Oui

# SSH par clé uniquement
sed -i 's/^#\?PasswordAuthentication.*/PasswordAuthentication no/' /etc/ssh/sshd_config
systemctl reload ssh

# Pare-feu : SSH, 80 (certificat) et 443 seulement
cat > /etc/nftables.conf <<'EOF'
#!/usr/sbin/nft -f
flush ruleset
table inet filter {
  chain input {
    type filter hook input priority 0; policy drop;
    iif lo accept
    ct state established,related accept
    meta l4proto { icmp, ipv6-icmp } accept
    tcp dport { 22, 80, 443 } accept
  }
}
EOF
systemctl enable --now nftables
```

### 3. Programme et clé Trocador

Sur le Mac :

```sh
cd relay
GOOS=linux GOARCH=amd64 CGO_ENABLED=0 go build -trimpath -ldflags='-s -w' -o biscuit-relay-linux .
scp biscuit-relay-linux root@SERVEUR:/usr/local/bin/biscuit-relay
scp deploy/biscuit-relay.service root@SERVEUR:/etc/systemd/system/
```

(`GOARCH=arm64` si le VPS est en ARM.)

Sur le serveur, la clé Trocador (tapée, jamais dans l'historique du shell) :

```sh
chmod 755 /usr/local/bin/biscuit-relay
install -d -m 700 /etc/biscuit-relay
read -rs K && printf '%s\n' "$K" > /etc/biscuit-relay/trocador-key && unset K
chmod 600 /etc/biscuit-relay/trocador-key
```

### 4. Service

Dans `/etc/systemd/system/biscuit-relay.service`, vérifier les domaines et remplacer
`age1REPLACE_WITH_PUBLIC_KEY` par la clé publique, puis :

```sh
systemctl daemon-reload
systemctl enable --now biscuit-relay
journalctl -u biscuit-relay -f        # doit afficher "serving https://..."
```

Depuis le Mac : `curl https://DOMAINE/health` doit répondre `ok`.

### Site web (même programme)

Le relais sert aussi le site statique : `-site-domain biscuitwallet.com -site-dir
/srv/biscuit-site`. Le relais répond seulement sur `relay.` (`/api/`, `/health`), le
site sur le domaine principal, `www.` redirige vers le domaine principal, tout le
reste répond 404. Pas de proxy inverse : l'IP réelle arrive directement au relais, et
le site n'enregistre rien (aucun journal d'accès). Pas de listes de dossiers ni de
fichiers cachés (`.git`…), GET/HEAD seulement. Enregistrements DNS `A` pour
`relay`, `@` et `www`. Le certificat de chaque nom est obtenu à la première visite.

Mettre en ligne le site depuis le Mac (fichiers lisibles par tous, le service tourne
sous un utilisateur dynamique) :

```sh
rsync -a --delete --chmod=D755,F644 site/ root@SERVEUR:/srv/biscuit-site/
```

### 5. Surveillance

Un service externe de vérification de disponibilité qui appelle
`https://DOMAINE/health` toutes les 5 minutes et envoie un email en cas de panne.
Si le relais tombe, seuls les échanges Trocador s'arrêtent ; le wallet et les swaps
atomiques continuent.

## Entretien

- **Mettre à jour le programme** : recompiler, `scp`, puis
  `systemctl restart biscuit-relay`.
- **Changer la clé Trocador** : réécrire `/etc/biscuit-relay/trocador-key` comme à
  l'étape 3, puis `systemctl restart biscuit-relay`. Aucune mise à jour de l'app.
- Le système se met à jour tout seul ; redémarrer le serveur de temps en temps
  (`reboot`) pour les mises à jour du noyau.

## Répondre à une demande légale

Trocador transmet les demandes par email depuis une adresse **@trocador.app**.

1. **Vérifier l'origine** : un email peut être falsifié. Répondre en écrivant soi-même
   à l'adresse Trocador connue (pas via « Répondre »), ou vérifier la signature DKIM.
2. Copier les jours concernés sur le Mac :
   `scp root@SERVEUR:/var/lib/private/biscuit-relay/trades/2026-09-26.log .`
3. Déchiffrer hors ligne et chercher l'échange :
   `./biscuit-relay decrypt -identity trades-identity.txt 2026-09-26.log | grep ID_ECHANGE`
4. Envoyer **uniquement** la ligne de l'échange demandé, puis effacer les copies locales.

## Contrat avec l'app

- Même chemins et paramètres que `https://trocador.app/api/`, sans en-tête `API-Key`.
- Erreurs du relais : JSON `{"error": code, "message": texte}`. Le code `tor_exit`
  (403) n'est **pas** une clé refusée : l'app doit l'afficher sans couper les swaps.
- Trocador répond `{"error": "Invalid API key"}` (HTTP 404) quand la clé est refusée ;
  la réponse est transmise telle quelle.
- User agent et langue envoyés par l'app : ce sont eux qui sont journalisés.
