// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2011 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file license.txt or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_WALLET_H
#define BITCOIN_WALLET_H

#include "btc/bignum.h"
#include "btc/key.h"
#include "btc/script.h"

#include "btcNode/Block.h"
#include "btcNode/BlockChain.h"

#include "btcWallet/walletdb.h"

#include <boost/bind.hpp>

class Wallet;

class CWalletTx;
class CReserveKey;
class CKeyPool;

class CWalletDB;

class Wallet : public CCryptoKeyStore
{
private:
    bool SelectCoinsMinConf(int64 nTargetValue, int nConfMine, int nConfTheirs, std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet, int64& nValueRet) const;
    bool SelectCoins(int64 nTargetValue, std::set<std::pair<const CWalletTx*,unsigned int> >& setCoinsRet, int64& nValueRet) const;

    CWalletDB *pwalletdbEncryption;

public:
    mutable CCriticalSection cs_wallet;

    bool fFileBacked;
    std::string strWalletFile;

    std::set<int64> setKeyPool;

    typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
    MasterKeyMap mapMasterKeys;
    unsigned int nMasterKeyMaxID;

    Wallet(BlockChain& blockChain) : _blockChain(blockChain) {
        fFileBacked = false;
        nMasterKeyMaxID = 0;
        pwalletdbEncryption = NULL;
        
        _blockChain.onAcceptBlock = blockAccepted;
        _blockChain.onAcceptTransaction = transactionAccepted;
    }
    
    Wallet(BlockChain& blockChain, std::string walletFile) : _blockChain(blockChain) {
        strWalletFile = walletFile;
        fFileBacked = true;
        nMasterKeyMaxID = 0;
        pwalletdbEncryption = NULL;
        
        _blockChain.onAcceptBlock = blockAccepted;
        _blockChain.onAcceptTransaction = transactionAccepted;
    }

    void transactionAccepted(Transaction& t, Block& b) {
        AddToWalletIfInvolvingMe(t, &b, true);
    }
    void blockAccepted(Block& b) {
        TransactionList txes = b.getTransactions();
        for(TransactionList::const_iterator tx = txes.begin(); tx != txes.end(); ++tx)
            AddToWalletIfInvolvingMe(*tx, NULL, true);
    }
    
    std::map<uint256, CWalletTx> mapWallet;
    std::vector<uint256> vWalletUpdated;

    std::map<uint256, int> mapRequestCount;

    std::map<CBitcoinAddress, std::string> mapAddressBook;

    std::vector<unsigned char> vchDefaultKey;

    // keystore implementation
    bool AddKey(const CKey& key);
    bool LoadKey(const CKey& key) { return CCryptoKeyStore::AddKey(key); }
    bool AddCryptedKey(const std::vector<unsigned char> &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret);
    bool LoadCryptedKey(const std::vector<unsigned char> &vchPubKey, const std::vector<unsigned char> &vchCryptedSecret) { return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret); }

    bool Unlock(const std::string& strWalletPassphrase);
    bool ChangeWalletPassphrase(const std::string& strOldWalletPassphrase, const std::string& strNewWalletPassphrase);
    bool EncryptWallet(const std::string& strWalletPassphrase);

    bool AddToWallet(const CWalletTx& wtxIn);
    bool AddToWalletIfInvolvingMe(const Transaction& tx, const Block* pblock, bool fUpdate = false);
    bool EraseFromWallet(uint256 hash);
    void WalletUpdateSpent(const Transaction& prevout);
    int ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate = false);
    void ReacceptWalletTransactions();
    void ResendWalletTransactions();
    int64 GetBalance() const;
    bool CreateTransaction(const std::vector<std::pair<CScript, int64> >& vecSend, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet);
    bool CreateTransaction(CScript scriptPubKey, int64 nValue, CWalletTx& wtxNew, CReserveKey& reservekey, int64& nFeeRet);
    bool CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey);
    bool BroadcastTransaction(CWalletTx& wtxNew);
    std::string SendMoney(CScript scriptPubKey, int64 nValue, CWalletTx& wtxNew, bool fAskFee=false);
    std::string SendMoneyToBitcoinAddress(const CBitcoinAddress& address, int64 nValue, CWalletTx& wtxNew, bool fAskFee=false);

    bool TopUpKeyPool();
    void ReserveKeyFromKeyPool(int64& nIndex, CKeyPool& keypool);
    void KeepKey(int64 nIndex);
    void ReturnKey(int64 nIndex);
    bool GetKeyFromPool(std::vector<unsigned char> &key, bool fAllowReuse=true);
    int64 GetOldestKeyPoolTime();

    bool IsMine(const CTxIn& txin) const;
    int64 GetDebit(const CTxIn& txin) const;
    bool IsMine(const CTxOut& txout) const
    {
        return ::IsMine(*this, txout.scriptPubKey);
    }
    int64 GetCredit(const CTxOut& txout) const
    {
        if (!MoneyRange(txout.nValue))
            throw std::runtime_error("Wallet::GetCredit() : value out of range");
        return (IsMine(txout) ? txout.nValue : 0);
    }
    bool IsChange(const CTxOut& txout) const
    {
        CBitcoinAddress address;
        if (ExtractAddress(txout.scriptPubKey, this, address))
            CRITICAL_BLOCK(cs_wallet)
                if (!mapAddressBook.count(address))
                    return true;
        return false;
    }
    int64 GetChange(const CTxOut& txout) const
    {
        if (!MoneyRange(txout.nValue))
            throw std::runtime_error("Wallet::GetChange() : value out of range");
        return (IsChange(txout) ? txout.nValue : 0);
    }
    bool IsMine(const Transaction& tx) const
    {
        BOOST_FOREACH(const CTxOut& txout, tx.vout)
            if (IsMine(txout))
                return true;
        return false;
    }
    bool IsFromMe(const Transaction& tx) const
    {
        return (GetDebit(tx) > 0);
    }
    int64 GetDebit(const Transaction& tx) const
    {
        int64 nDebit = 0;
        BOOST_FOREACH(const CTxIn& txin, tx.vin)
        {
            nDebit += GetDebit(txin);
            if (!MoneyRange(nDebit))
                throw std::runtime_error("Wallet::GetDebit() : value out of range");
        }
        return nDebit;
    }
    int64 GetCredit(const Transaction& tx) const
    {
        int64 nCredit = 0;
        BOOST_FOREACH(const CTxOut& txout, tx.vout)
        {
            nCredit += GetCredit(txout);
            if (!MoneyRange(nCredit))
                throw std::runtime_error("Wallet::GetCredit() : value out of range");
        }
        return nCredit;
    }
    int64 GetChange(const Transaction& tx) const
    {
        int64 nChange = 0;
        BOOST_FOREACH(const CTxOut& txout, tx.vout)
        {
            nChange += GetChange(txout);
            if (!MoneyRange(nChange))
                throw std::runtime_error("Wallet::GetChange() : value out of range");
        }
        return nChange;
    }
    void SetBestChain(const CBlockLocator& loc);

    int LoadWallet(bool& fFirstRunRet);
//    bool BackupWallet(const std::string& strDest);

    bool SetAddressBookName(const CBitcoinAddress& address, const std::string& strName);

    bool DelAddressBookName(const CBitcoinAddress& address);

    void UpdatedTransaction(const uint256 &hashTx)
    {
        CRITICAL_BLOCK(cs_wallet)
            vWalletUpdated.push_back(hashTx);
    }

    void PrintWallet(const Block& block);

    void Inventory(const uint256 &hash)
    {
        CRITICAL_BLOCK(cs_wallet)
        {
            std::map<uint256, int>::iterator mi = mapRequestCount.find(hash);
            if (mi != mapRequestCount.end())
                (*mi).second++;
        }
    }

    int GetKeyPoolSize()
    {
        return setKeyPool.size();
    }

    bool GetTransaction(const uint256 &hashTx, CWalletTx& wtx);

    bool SetDefaultKey(const std::vector<unsigned char> &vchPubKey);
    
    void Sync();
private:
    BlockChain& _blockChain;
};


class CReserveKey
{
protected:
    Wallet* pwallet;
    int64 nIndex;
    std::vector<unsigned char> vchPubKey;
public:
    CReserveKey(Wallet* pwalletIn)
    {
        nIndex = -1;
        pwallet = pwalletIn;
    }

    ~CReserveKey()
    {
        if (!fShutdown)
            ReturnKey();
    }

    void ReturnKey();
    std::vector<unsigned char> GetReservedKey();
    void KeepKey();
};


//
// Private key that includes an expiration date in case it never gets used.
//
class CWalletKey
{
public:
    CPrivKey vchPrivKey;
    int64 nTimeCreated;
    int64 nTimeExpires;
    std::string strComment;
    //// todo: add something to note what created it (user, getnewaddress, change)
    ////   maybe should have a map<string, string> property map

    CWalletKey(int64 nExpires=0)
    {
        nTimeCreated = (nExpires ? GetTime() : 0);
        nTimeExpires = nExpires;
    }

    IMPLEMENT_SERIALIZE
    (
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPrivKey);
        READWRITE(nTimeCreated);
        READWRITE(nTimeExpires);
        READWRITE(strComment);
    )
};






//
// Account information.
// Stored in wallet with key "acc"+string account name
//
class CAccount
{
public:
    std::vector<unsigned char> vchPubKey;

    CAccount()
    {
        SetNull();
    }

    void SetNull()
    {
        vchPubKey.clear();
    }

    IMPLEMENT_SERIALIZE
    (
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        READWRITE(vchPubKey);
    )
};



//
// Internal transfers.
// Database key is acentry<account><counter>
//
class CAccountingEntry
{
public:
    std::string strAccount;
    int64 nCreditDebit;
    int64 nTime;
    std::string strOtherAccount;
    std::string strComment;

    CAccountingEntry()
    {
        SetNull();
    }

    void SetNull()
    {
        nCreditDebit = 0;
        nTime = 0;
        strAccount.clear();
        strOtherAccount.clear();
        strComment.clear();
    }

    IMPLEMENT_SERIALIZE
    (
        if (!(nType & SER_GETHASH))
            READWRITE(nVersion);
        // Note: strAccount is serialized as part of the key, not here.
        READWRITE(nCreditDebit);
        READWRITE(nTime);
        READWRITE(strOtherAccount);
        READWRITE(strComment);
    )
};

bool GetWalletFile(Wallet* pwallet, std::string &strWalletFileOut);

#endif
