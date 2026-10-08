// SPDX-License-Identifier: BSD-3-Clause
// SPDX-FileCopyrightText: The Biscuit developers

// Replies of Ethereum nodes (JSON-RPC) and of Blockscout, as received on
// 2026-10-08 (trimmed to the fields Biscuit reads), and the cases that must
// never show up wrong: fake tokens, failed or pending transactions, replies
// that do not have the expected shape.

#include <QJsonDocument>
#include <QtTest>

#include "EthApi.h"

using namespace biscuit::coins;
using namespace biscuit::coins::eth;

namespace {
    const QByteArray me = QByteArray::fromHex("d8da6bf26964af9d7eed9e03e53415d37aa96045");
    const QString meHex = "0xd8dA6BF26964aF9D7eEd9e03E53415D37aA96045";
    const QString other = "0x64b8024a2C265A70583D52e4f9FDb0A83f6c6209";
    const QString usdt = "0xdAC17F958D2ee523a2206206994597C13D831ec7";

    QJsonValue json(const char *text) {
        return QJsonDocument::fromJson(text).object();
    }
    QJsonObject party(const QString &address) { return {{"hash", address}}; }
    QString hash(char c) { return "0x" + QString(64, QChar(c)); }

    QJsonObject tx(const QString &h, const QString &from, const QString &to, const QString &value,
                   const QString &status = "ok", const QString &rawInput = "0x") {
        return {{"hash", h}, {"status", status}, {"timestamp", "2026-10-06T17:34:11.000000Z"},
                {"block_number", 26134855}, {"value", value}, {"fee", QJsonObject{{"type", "actual"}, {"value", "51151897817048"}}},
                {"raw_input", rawInput}, {"from", party(from)}, {"to", party(to)}};
    }
    QJsonObject transfer(const QString &h, const QString &contract, const QString &from, const QString &to,
                         const QString &value, const QString &decimals = "6") {
        return {{"transaction_hash", h}, {"timestamp", "2026-09-19T17:22:23.000000Z"}, {"block_number", 26013023},
                {"log_index", 144}, {"type", "token_transfer"},
                {"total", QJsonObject{{"decimals", decimals}, {"value", value}}},
                {"token", QJsonObject{{"address_hash", contract}, {"symbol", "USDT"}, {"decimals", decimals}, {"type", "ERC-20"}}},
                {"from", party(from)}, {"to", party(to)}};
    }
    QJsonObject page(const QJsonArray &items) { return {{"items", items}, {"next_page_params", QJsonValue()}}; }
}

class TestEthApi : public QObject
{
Q_OBJECT

private slots:
    void rpcReplies() {
        QCOMPARE(rpc::request(3, "eth_chainId").value("method").toString(), QString("eth_chainId"));
        QString error;
        QCOMPARE(rpc::result(json(R"({"jsonrpc":"2.0","result":"0x1","id":1})"), 1, &error)->toString(), QString("0x1"));
        // A reply to another request, an error, or no result at all.
        QVERIFY(!rpc::result(json(R"({"jsonrpc":"2.0","result":"0x1","id":2})"), 1).has_value());
        QVERIFY(!rpc::result(json(R"({"jsonrpc":"2.0","error":{"code":-32046,"message":"Cannot fulfill request"},"id":1})"), 1, &error).has_value());
        QCOMPARE(error, QString("Cannot fulfill request"));
        QVERIFY(!rpc::result(json(R"({"jsonrpc":"2.0","id":1})"), 1).has_value());

        // Batch replies come in any order.
        const QJsonArray batch = QJsonDocument::fromJson(R"([{"jsonrpc":"2.0","id":2,"result":"0xa"},{"jsonrpc":"2.0","id":1,"result":"0x1"}])").array();
        QCOMPARE(rpc::batchResult(batch, 1)->toString(), QString("0x1"));
        QCOMPARE(rpc::batchResult(batch, 2)->toString(), QString("0xa"));
        QVERIFY(!rpc::batchResult(batch, 3).has_value());
    }

    void feeHistory() {
        // publicnode.com, 2026-10-08: base fee of the next block 0.58 gwei.
        const auto fees = parseFeeHistory(json(R"({"baseFeePerGas":["0x23292dac","0x21e17641","0x21692f77","0x2210adfb","0x2381c98d","0x22a392d6"],"gasUsedRatio":[0.35,0.44,0.57,0.66,0.40],"oldestBlock":"0x18efc08","reward":[["0x1183c0c9"],["0x1f9021fb"],["0xf70c656"],["0xf70c656"],["0x8f0d180"]]})"));
        QVERIFY(fees.has_value());
        QCOMPARE(formatAmount(fees->baseFee, 0), QString("581145302"));
        QCOMPARE(formatAmount(fees->priorityFee, 0), QString("259049046"));    // median tip
        QCOMPARE(formatAmount(fees->maxFeePerGas, 0), QString("1421339650"));  // 2 x base + tip

        // No tips reported: the minimum tip.
        const auto quiet = parseFeeHistory(json(R"({"baseFeePerGas":["0x1","0x2"],"reward":[["0x0"]]})"));
        QCOMPARE(formatAmount(quiet->priorityFee, 0), formatAmount(minimumPriorityFee, 0));
        QVERIFY(!parseFeeHistory(json(R"({"baseFeePerGas":[],"reward":[]})")).has_value());
        QVERIFY(!parseFeeHistory(json(R"({"baseFeePerGas":["12"],"reward":[]})")).has_value());   // not a quantity
    }

    void paths() {
        QCOMPARE(blockscout::transactionsPath(me), QString("/api/v2/addresses/%1/transactions").arg(meHex));
        QCOMPARE(blockscout::tokenTransfersPath(me, tokens().at(0)),
                 QString("/api/v2/addresses/%1/token-transfers?type=ERC-20&token=%2").arg(meHex, usdt));
    }

    void ethHistory() {
        // Real incoming payment to vitalik.eth (0.000165970070876188 ETH).
        const auto list = blockscout::parseTransactions(page({
            tx("0xf40630389e1a043942a175458d14f2d830a32c3d3d6f80200114f8dd304f1f18", other, meHex, "165970070876188"),
            tx(hash('b'), meHex, other, "1000000000000000000"),          // sent 1 ETH
            tx(hash('c'), other, meHex, "0"),                            // incoming call, nothing received
            tx(hash('d'), other, meHex, "5", "error"),                    // failed: nothing received
            tx(hash('e'), other, "0x5aAeb6053F3E94C9b9A09f33669435E7Ef1BeAed", "7"),   // not ours
        }), me);
        QVERIFY(list.has_value());
        QCOMPARE(list->size(), 2);
        const HistoryEntry &in = list->at(0);
        QCOMPARE(in.asset, QString("ETH"));
        QVERIFY(in.incoming);
        QCOMPARE(formatAmount(in.amount, 18), QString("0.000165970070876188"));
        QCOMPARE(in.fee, u128(0));   // paid by the sender
        QCOMPARE(in.counterparty.toHex(), QByteArray("64b8024a2c265a70583d52e4f9fdb0a83f6c6209"));
        QCOMPARE(in.block, quint64(26134855));
        QCOMPARE(in.time, QDateTime(QDate(2026, 10, 6), QTime(17, 34, 11), QTimeZone::UTC));
        const HistoryEntry &out = list->at(1);
        QVERIFY(!out.incoming);
        QCOMPARE(formatAmount(out.fee, 18), QString("0.000051151897817048"));
    }

    void pendingAndFailed() {
        QJsonObject pending = tx(hash('a'), meHex, other, "5");
        pending["block_number"] = QJsonValue();
        pending["timestamp"] = QJsonValue();
        pending["status"] = QJsonValue();
        const auto list = blockscout::parseTransactions(page({pending, tx(hash('b'), meHex, other, "5", "error")}), me);
        QVERIFY(list.has_value());
        QCOMPARE(list->at(0).block, quint64(0));
        QVERIFY(!list->at(0).time.isValid());
        QVERIFY(!list->at(0).failed);
        QVERIFY(list->at(1).failed);   // shown: its fee was paid
    }

    void tokenCallsInTransactions() {
        // A USDT transfer from us: its fee is kept for the token transfer,
        // shown from /token-transfers. A failed one is only here: shown.
        const QString data = "0xa9059cbb000000000000000000000000" + other.mid(2).toLower()
                             + "000000000000000000000000000000000000000000000000000000000012d687";
        QHash<QByteArray, u128> fees;
        const auto list = blockscout::parseTransactions(page({
            tx(hash('a'), meHex, usdt, "0", "ok", data),
            tx(hash('b'), meHex, usdt, "0", "error", data),
        }), me, &fees);
        QVERIFY(list.has_value());
        QCOMPARE(fees.value(QByteArray::fromHex(hash('a').mid(2).toLatin1())), u128(51151897817048ULL));
        QCOMPARE(list->size(), 1);
        QCOMPARE(list->at(0).asset, QString("USDT"));
        QVERIFY(list->at(0).failed);
        QCOMPARE(formatAmount(list->at(0).amount, 6), QString("1.234567"));
        QCOMPARE(list->at(0).counterparty.toHex(), other.mid(2).toLower().toLatin1());
    }

    void tokenTransfers() {
        const Token &token = tokens().at(0);   // USDT
        const auto list = blockscout::parseTokenTransfers(page({
            transfer(hash('a'), usdt, other, meHex, "1000000"),                                  // received 1 USDT
            transfer(hash('b'), usdt, meHex, other, "2500000"),                                  // sent 2.5 USDT
            transfer(hash('c'), "0x8e7BD6BFBd8f0F8d5ab75c05fFdaBa94f6DD1ecD", other, meHex, "1000000"),   // fake "USDT"
        }), me, token);
        QVERIFY(list.has_value());
        QCOMPARE(list->size(), 2);
        QVERIFY(list->at(0).incoming);
        QCOMPARE(formatAmount(list->at(0).amount, token.decimals), QString("1"));
        QVERIFY(!list->at(1).incoming);
        QCOMPARE(formatAmount(list->at(1).amount, token.decimals), QString("2.5"));
    }

    void refusesMalformedPages() {
        // Anything that does not look like Blockscout refuses the page, rather
        // than showing a wrong amount.
        QVERIFY(!blockscout::parseTransactions(json(R"({"message":"Not found"})"), me).has_value());
        QVERIFY(!blockscout::parseTransactions(page({tx("0x1234", other, meHex, "5")}), me).has_value());       // short hash
        QVERIFY(!blockscout::parseTransactions(page({tx(hash('a'), other, meHex, "-5")}), me).has_value());     // negative
        QVERIFY(!blockscout::parseTransactions(page({tx(hash('a'), other, meHex, "1.5")}), me).has_value());    // not wei
        QVERIFY(!blockscout::parseTransactions(page({tx(hash('a'), "0x1234", meHex, "5")}), me).has_value());   // bad address
        // Token decimals that are not USDT's: not the real token.
        QVERIFY(!blockscout::parseTokenTransfers(page({transfer(hash('a'), usdt, other, meHex, "1", "18")}), me, tokens().at(0)).has_value());
    }
};

QTEST_GUILESS_MAIN(TestEthApi)
#include "test_eth_api.moc"
