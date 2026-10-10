// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Ethereum keys, addresses, amounts, RLP, EIP-1559 transactions and ERC-20,
// checked against the official vectors (Keccak, EIP-55, EIP-155, RLP wiki)
// and transactions signed by ethers.js 6.17 with the same keys and fields.

#include <QtTest>

#include "Bip39.h"
#include "Eth.h"

using namespace biscuit::coins;
using namespace biscuit::coins::eth;

namespace {
    const QString abandonAbout = "abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon abandon about";
    const QByteArray key46 = QByteArray(32, '\x46');
    const QByteArray to35 = QByteArray(20, '\x35');

    QByteArray hex(const char *h) { return QByteArray::fromHex(h); }
    QString str(u128 v) { return formatAmount(v, 0); }
}

class TestEth : public QObject
{
Q_OBJECT

private slots:
    // ---------------------------------------------------------- Keccak-256

    void keccak() {
        // Ethereum's Keccak-256, not SHA3-256 (which gives a7ffc6f8… for "").
        QCOMPARE(keccak256({}).toHex(), QByteArray("c5d2460186f7233c927e7db2dcc703c0e500b653ca82273b7bfad8045d85a470"));
        QCOMPARE(keccak256("transfer(address,uint256)").left(4).toHex(), QByteArray("a9059cbb"));
        QCOMPARE(keccak256("balanceOf(address)").left(4).toHex(), QByteArray("70a08231"));
        // Longer than one block (136 bytes), from ethers.js.
        QCOMPARE(keccak256(QByteArray(200, 'a')).toHex(),
                 QByteArray("96ea54061def936c4be90b518992fdc6f12f535068a256229aca54267b4d084d"));
    }

    // ----------------------------------------------------------- addresses

    void eip55Vectors() {
        // From the EIP-55 specification.
        for (const char *a : {"0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed", "0xfB6916095ca1df60bB79Ce92cE3Ea74c37c5d359",
                              "0xdbF03B407c01E7cD3CBea99509d93f8DDDC8C6FB", "0xD1220A0cf47c7B9Be7A2E6BA89F429762e7b9aDb"}) {
            const QString address = QString::fromLatin1(a);
            const auto parsed = parseAddress(address);
            QVERIFY(parsed.has_value());
            QCOMPARE(checksumAddress(*parsed), address);
            // All lowercase or all uppercase: no checksum, accepted.
            QVERIFY(parseAddress("0x" + address.mid(2).toLower()).has_value());
            QVERIFY(parseAddress("0x" + address.mid(2).toUpper()).has_value());
        }
    }

    void addressTypos() {
        QString error;
        // One letter with the wrong case: a typo.
        QVERIFY(!parseAddress("0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAeD", &error).has_value());
        QVERIFY(error.contains("typo"));
        QVERIFY(!parseAddress("5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed").has_value());     // no 0x
        QVERIFY(!parseAddress("0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeA").has_value());     // too short
        QVERIFY(!parseAddress("0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAedd").has_value());  // too long
        QVERIFY(!parseAddress("0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAeg").has_value());   // not hex
        QVERIFY(!parseAddress("bc1qcr8te4kr609gcawutmrza0j4xv80jy8z306fyu").has_value());
        // Spaces around a pasted address are fine.
        QVERIFY(parseAddress("  0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed\n").has_value());
    }

    void addressFromKeys() {
        // web3.js documentation: private key -> address.
        QCOMPARE(checksumAddress(addressFromPrivateKey(hex("4c0883a69102937d6231471b5dbb6204fe5129617082792ae468d01a3f362318"))),
                 QString("0x2c7536E3605D9C16a7a3D7b1898e529396a65c23"));
        QCOMPARE(checksumAddress(addressFromPrivateKey(key46)), QString("0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F"));
        QVERIFY(addressFromPrivateKey(QByteArray(32, '\0')).isEmpty());   // not a valid key
        QVERIFY(addressFromPublicKey(QByteArray()).isEmpty());

        // The signer read back from a signed transaction is the same address.
        const auto signedTx = sign(Transaction{1, 0, 0, 1, 21000, to35, 0, {}}, key46);
        QVERIFY(signedTx.has_value());
        const auto decoded = decode(*signedTx);
        QVERIFY(decoded.has_value());
        QCOMPARE(checksumAddress(decoded->from), QString("0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F"));
    }

    void hdDerivation() {
        // "abandon … about" at m/44'/60'/0'/0/i: MetaMask, ethers.js and others.
        const auto seed = bip39::mnemonicToSeed(abandonAbout, {});
        QVERIFY(seed.has_value());
        const auto account = Account::fromSeed(*seed);
        QVERIFY(account.has_value());
        QCOMPARE(checksumAddress(account->address(0)), QString("0x9858EfFD232B4033E47d90003D41EC34EcaEda94"));
        QCOMPARE(account->privateKey(0).toHex(), QByteArray("1ab42cc412b618bdea3a599e3c9bae199ebf030895b039e9db1e30dafb12b727"));
        QCOMPARE(checksumAddress(account->address(1)), QString("0x6Fac4D18c912343BF86fa7049364Dd4E424Ab9C0"));
        QCOMPARE(account->privateKey(1).toHex(), QByteArray("9a983cb3d832fbde5ab49d692b7a8bf5b5d232479c99333d0fc8e1d21f1b55b6"));
    }

    // ------------------------------------------------------------- amounts

    void amounts() {
        QCOMPARE(str(*parseAmount("1", 18)), QString("1000000000000000000"));
        QCOMPARE(str(*parseAmount("1.5", 18)), QString("1500000000000000000"));
        QCOMPARE(str(*parseAmount("0.000000000000000001", 18)), QString("1"));
        QCOMPARE(str(*parseAmount(".5", 6)), QString("500000"));
        QCOMPARE(str(*parseAmount("1.234567", 6)), QString("1234567"));
        QCOMPARE(str(*parseAmount("120000000", 18)), QString("120000000000000000000000000"));   // all the ETH there is
        QVERIFY(!parseAmount("1.2345678", 6).has_value());   // more decimals than USDT has
        QVERIFY(!parseAmount("-1", 18).has_value());
        QVERIFY(!parseAmount("1e18", 18).has_value());
        QVERIFY(!parseAmount("1,5", 18).has_value());
        QVERIFY(!parseAmount("", 18).has_value());
        QVERIFY(!parseAmount(".", 18).has_value());
        QVERIFY(!parseAmount("1.2.3", 18).has_value());
        QVERIFY(!parseAmount("999999999999999999999", 18).has_value());   // over 128 bits

        QCOMPARE(formatAmount(*parseAmount("1.5", 18), 18), QString("1.5"));
        QCOMPARE(formatAmount(1, 18), QString("0.000000000000000001"));
        QCOMPARE(formatAmount(0, 18), QString("0"));
        QCOMPARE(formatAmount(1234567, 6), QString("1.234567"));
        QCOMPARE(formatAmount(1000000, 6), QString("1"));
        QCOMPARE(formatAmount(~u128(0), 0), QString("340282366920938463463374607431768211455"));
    }

    void quantities() {
        QCOMPARE(str(*parseQuantity("0x0")), QString("0"));
        QCOMPARE(str(*parseQuantity("0xde0b6b3a7640000")), QString("1000000000000000000"));
        QCOMPARE(str(*parseQuantity("0xffffffffffffffffffffffffffffffff")), QString("340282366920938463463374607431768211455"));
        QVERIFY(!parseQuantity("0x100000000000000000000000000000000").has_value());   // 129 bits
        QVERIFY(!parseQuantity("0x01").has_value());   // leading zero
        QVERIFY(!parseQuantity("0x").has_value());
        QVERIFY(!parseQuantity("12").has_value());
        QVERIFY(!parseQuantity("0xg").has_value());
        QCOMPARE(toQuantity(0), QString("0x0"));
        QCOMPARE(toQuantity(255), QString("0xff"));
        QCOMPARE(toQuantity(*parseAmount("1", 18)), QString("0xde0b6b3a7640000"));

        QByteArray word(32, '\0');
        word[31] = '\x2a';
        QCOMPARE(str(*parseUint256(word)), QString("42"));
        word[15] = '\x01';   // over 128 bits: refused, not truncated
        QVERIFY(!parseUint256(word).has_value());
        QVERIFY(!parseUint256(QByteArray(31, '\0')).has_value());
    }

    // ----------------------------------------------------------------- RLP

    void rlpVectors() {
        // From the Ethereum RLP documentation.
        QCOMPARE(rlp::bytes("dog").toHex(), QByteArray("83646f67"));
        QCOMPARE(rlp::list({rlp::bytes("cat"), rlp::bytes("dog")}).toHex(), QByteArray("c88363617483646f67"));
        QCOMPARE(rlp::bytes({}).toHex(), QByteArray("80"));
        QCOMPARE(rlp::list({}).toHex(), QByteArray("c0"));
        QCOMPARE(rlp::uint(0).toHex(), QByteArray("80"));
        QCOMPARE(rlp::bytes(QByteArray(1, '\0')).toHex(), QByteArray("00"));
        QCOMPARE(rlp::uint(15).toHex(), QByteArray("0f"));
        QCOMPARE(rlp::uint(1024).toHex(), QByteArray("820400"));
        QCOMPARE(rlp::list({rlp::list({}), rlp::list({rlp::list({})}), rlp::list({rlp::list({}), rlp::list({rlp::list({})})})}).toHex(),
                 QByteArray("c7c0c1c0c3c0c1c0"));
        const QByteArray lorem = "Lorem ipsum dolor sit amet, consectetur adipisicing elit";
        QCOMPARE(rlp::bytes(lorem).toHex(), QByteArray("b838") + lorem.toHex());

        // Decoding is strict: non-canonical encodings are refused.
        QCOMPARE(rlp::decode(hex("83646f67"))->payload, QByteArray("dog"));
        QVERIFY(rlp::decode(hex("c88363617483646f67"))->isList);
        QVERIFY(!rlp::decode(hex("8105")).has_value());          // 05 must encode as itself
        QVERIFY(!rlp::decode(hex("b803646f67")).has_value());    // long form for a short string
        QVERIFY(!rlp::decode(hex("83646f")).has_value());        // truncated
        QVERIFY(!rlp::decode(hex("83646f6767")).has_value());    // trailing byte
    }

    // -------------------------------------------------------- transactions

    void eip1559Ether() {
        // Same fields signed by ethers.js: 1 ETH, nonce 9, 1 / 30 gwei.
        Transaction tx;
        tx.chainId = 1;
        tx.nonce = 9;
        tx.maxPriorityFeePerGas = 1000000000;
        tx.maxFeePerGas = 30000000000ULL;
        tx.gasLimit = transferGas;
        tx.to = to35;
        tx.value = *parseAmount("1", etherDecimals);
        QCOMPARE(tx.unsignedSerialized().toHex(), QByteArray("02f00109843b9aca008506fc23ac00825208943535353535353535353535353535353535353535880de0b6b3a764000080c0"));
        QCOMPARE(tx.signingHash().toHex(), QByteArray("61b515f42ee083d21625f1465de93314f0df919cee4e9689347e3e6387f4eeea"));

        const auto raw = sign(tx, key46);
        QVERIFY(raw.has_value());
        QCOMPARE(raw->toHex(), QByteArray("02f8730109843b9aca008506fc23ac00825208943535353535353535353535353535353535353535880de0b6b3a764000080c001a0ad4241a480b4069450d9bd1823a3afb8bd5a697e2135056f13fc85778785a013a0782c2fc38f01d18de96e4f5caeb077484ef6d844f2ce873a475515552460c075"));

        const auto decoded = decode(*raw);
        QVERIFY(decoded.has_value());
        QVERIFY(decoded->tx == tx);
        QCOMPARE(checksumAddress(decoded->from), QString("0x9d8A62f656a8d1615C1294fd71e9CFb3E4855A4F"));
        QCOMPARE(decoded->hash.toHex(), QByteArray("8a3ff34f002402f5d26a11e21f0efcd1810e189a4ba6555cf013579c8ac06727"));
    }

    void eip1559Zeros() {
        // Zero nonce, tip and value: empty RLP integers. Signature with y = 0.
        const Transaction tx{1, 0, 0, 1, transferGas, to35, 0, {}};
        QCOMPARE(tx.unsignedSerialized().toHex(), QByteArray("02df018080018252089435353535353535353535353535353535353535358080c0"));
        const auto raw = sign(tx, key46);
        QVERIFY(raw.has_value());
        QCOMPARE(raw->toHex(), QByteArray("02f862018080018252089435353535353535353535353535353535353535358080c080a0d9c00f630d46b3049044be11bc37992fb4e94b6b86d9d47536462f96513918f0a05fc98727c980b516529df6da180584f5cefce1f258341ed9d3c4306863bddcfe"));
        QCOMPARE(decode(*raw)->hash.toHex(), QByteArray("6f293b776c420a08089334e4b70afdad40822ac183b66e761ab28ff0edcd33c4"));
    }

    void eip1559Usdt() {
        // 1.234567 USDT, nonce 300, 2 / 45 gwei, 65000 gas, as ethers.js signs it.
        const Token *usdt = tokenByContract(hex("dac17f958d2ee523a2206206994597c13d831ec7"));
        QVERIFY(usdt);
        QCOMPARE(usdt->symbol, QString("USDT"));
        QCOMPARE(usdt->decimals, 6);
        const QByteArray data = erc20TransferData(to35, *parseAmount("1.234567", usdt->decimals));
        QCOMPARE(data.toHex(), QByteArray("a9059cbb0000000000000000000000003535353535353535353535353535353535353535000000000000000000000000000000000000000000000000000000000012d687"));

        const Transaction tx{1, 300, 2000000000, 45000000000ULL, 65000, usdt->contract, 0, data};
        const auto raw = sign(tx, key46);
        QVERIFY(raw.has_value());
        QCOMPARE(raw->toHex(), QByteArray("02f8b20182012c8477359400850a7a35820082fde894dac17f958d2ee523a2206206994597c13d831ec780b844a9059cbb0000000000000000000000003535353535353535353535353535353535353535000000000000000000000000000000000000000000000000000000000012d687c080a0f61dadbce37be3ce2af10c9470db59e43aae2974e9ae7d5609c0823c2eacbc48a004884aa6604af597e4f9d2ad7411c12688619da948fc578d05e6a9fd0f10bc1d"));

        // The final check reads the recipient and amount back from the bytes.
        const auto decoded = decode(*raw);
        QVERIFY(decoded.has_value());
        QCOMPARE(decoded->hash.toHex(), QByteArray("26aa1834362352be95e6d64ebfb0ff49e929609ca3287ce58aeec3a81c5d4ffd"));
        const auto transfer = decodeErc20Transfer(decoded->tx.data);
        QVERIFY(transfer.has_value());
        QCOMPARE(transfer->first, to35);
        QCOMPARE(formatAmount(transfer->second, 6), QString("1.234567"));
    }

    void decodeRefusesTampering() {
        const auto raw = sign(Transaction{1, 9, 1, 30, transferGas, to35, 5, {}}, key46);
        QVERIFY(raw.has_value());
        // A changed byte (here the recipient) gives another sender, or no valid signature.
        QByteArray changed = *raw;
        changed[changed.indexOf(to35) + 3] = '\x36';
        const auto decoded = decode(changed);
        QVERIFY(!decoded.has_value() || decoded->from != decode(*raw)->from);
        QVERIFY(!decode(QByteArray::fromHex("01") + raw->mid(1)).has_value());   // not type 2
        QVERIFY(!decode(raw->left(raw->size() - 1)).has_value());               // truncated
        QVERIFY(!decode({}).has_value());
        // A recipient that is not 20 bytes is never signed.
        QVERIFY(!sign(Transaction{1, 0, 0, 1, transferGas, QByteArray(19, '\x35'), 0, {}}, key46).has_value());
    }

    // -------------------------------------------------------------- ERC-20

    void erc20() {
        QCOMPARE(tokens().size(), 2);
        QCOMPARE(checksumAddress(tokens().at(0).contract), QString("0xdAC17F958D2ee523a2206206994597C13D831ec7"));
        QCOMPARE(checksumAddress(tokens().at(1).contract), QString("0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48"));
        QVERIFY(!tokenByContract(to35));
        QCOMPARE(erc20BalanceOfData(to35).toHex(), QByteArray("70a082310000000000000000000000003535353535353535353535353535353535353535"));
        // Anything else than a plain transfer is not read as one.
        QVERIFY(!decodeErc20Transfer(erc20BalanceOfData(to35)).has_value());
        QVERIFY(!decodeErc20Transfer(erc20TransferData(to35, 1).left(67)).has_value());
        QByteArray dirty = erc20TransferData(to35, 1);
        dirty[5] = '\x01';   // garbage in the address padding
        QVERIFY(!decodeErc20Transfer(dirty).has_value());
    }
};

QTEST_GUILESS_MAIN(TestEth)
#include "test_eth.moc"
