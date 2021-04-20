/*

***********************************************************************************
* Copyright (C) 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef THREAD_SAFE_CONTAINERS_H
#define THREAD_SAFE_CONTAINERS_H

#include <mutex>
#include <unordered_map>

namespace bs {

   template<class K, class V>
   class ThreadSafeMap
   {
   public:
      void put(K key, V value)
      {
         std::lock_guard<std::mutex> lock(mutex_);
         data_[key] = std::move(value);
      }

      V take(const K &key)
      {
         std::lock_guard<std::mutex> lock(mutex_);
         auto it = data_.find(key);
         if (it == data_.end()) {
            return V{};
         }
         auto result = std::move(it->second);
         data_.erase(it);
         return result;
      }

      std::unordered_map<K, V> takeAll()
      {
         std::lock_guard<std::mutex> lock(mutex_);
         auto result = std::move(data_);
         return result;
      }

   private:
      std::mutex mutex_;
      std::unordered_map<K, V> data_;
   };

}

#endif // THREAD_SAFE_CONTAINERS_H
