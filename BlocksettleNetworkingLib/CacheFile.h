/*

***********************************************************************************
* Copyright (C) 2018 - 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef __CACHE_FILE_H__
#define __CACHE_FILE_H__

#include <unordered_map>
#include <atomic>
#include <thread>
#include <lmdbpp.h>
#include "AsyncClient.h"
#include "BinaryData.h"
#include "TxClasses.h"


class CacheFile
{
public:
   CacheFile(const std::string &filename, size_t nbElemLimit = 10000);
   ~CacheFile();

   void put(const BinaryData &key, const BinaryData &val);
   BinaryData get(const BinaryData &key) const;
   void stop();

protected:
   void read();
   void write();
   void saver();
   void purge();

private:
   const bool  inMem_;
   size_t      nbMaxElems_;
   LMDB     *  db_ = nullptr;
   std::shared_ptr<LMDBEnv>  dbEnv_;
   std::map<BinaryData, BinaryData> map_, mapModified_;
   std::thread thread_;
   std::condition_variable    cvSave_;
   mutable std::mutex         cvMutex_;
   mutable std::mutex         rwMutex_;
   std::atomic_bool           stopped_{ false };
};


class TxCacheFile : protected CacheFile
{
public:
   TxCacheFile(const std::string &filename, size_t nbElemLimit = 10000)
      : CacheFile(filename, nbElemLimit) {}

   void put(const BinaryData &key, const std::shared_ptr<const Tx> &tx);
   std::shared_ptr<const Tx> get(const BinaryData &key);

   void stop() { CacheFile::stop(); }

private:
   AsyncClient::TxBatchResult txMap_;
   std::mutex txMapMutex_;
};

#endif // __CACHE_FILE_H__
