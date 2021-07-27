/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef MESSAGE_BUS_H
#define MESSAGE_BUS_H

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <thread>
#include <vector>
#include "Message/Envelope.h"

namespace spdlog {
   class logger;
}

namespace bs {
   namespace message {
      class Adapter;

      class RouterInterface
      {
      public:
         virtual ~RouterInterface() = default;
         virtual void bindAdapter(const std::shared_ptr<Adapter> &) = 0;
         virtual std::vector<std::shared_ptr<bs::message::Adapter>> process(const Envelope &) const = 0;
         virtual void reset() = 0;
         virtual std::set<UserValue> supportedReceivers() const = 0;

      protected:
         virtual bool isDefaultRouted(const bs::message::Envelope &) const = 0;
      };

      class Router : public RouterInterface
      {
      public:
         Router(const std::shared_ptr<spdlog::logger> &);

         void bindAdapter(const std::shared_ptr<Adapter> &) override;
         std::set<UserValue> supportedReceivers() const override;
         std::vector<std::shared_ptr<bs::message::Adapter>> process(const Envelope &) const override;
         void reset() override;

      protected:
         bool isDefaultRouted(const bs::message::Envelope &) const override;

      private:
         std::shared_ptr<spdlog::logger>           logger_;
         mutable std::mutex mutex_;
         std::map<UserValue, std::shared_ptr<Adapter>>   adapters_;
         std::shared_ptr<Adapter>   supervisor_;
         std::shared_ptr<Adapter>   defaultRoute_;
      };

      class QueueInterface
      {
      public:
         QueueInterface(const std::shared_ptr<RouterInterface> &router
            , const std::string& name = {})
            : router_(router), name_(name) {}
         virtual ~QueueInterface() = default;

         virtual std::string name() const { return name_; }
         virtual void terminate() = 0;
         virtual void bindAdapter(const std::shared_ptr<Adapter> &) = 0;
         virtual std::set<UserValue> supportedReceivers() const = 0;

         virtual bool pushFill(Envelope &) = 0;
         SeqId nextId() { return seqNo_++; }
         SeqId resetId(SeqId);

         bool isCurrentlyProcessing(const Envelope& env) const
         {
            return (env.id() == currentEnvId_);
         }

      protected:
         virtual bool accept(const bs::message::Envelope&);
         auto defer(const Envelope& env) { return deferredIds_.insert(env.id()); }
         SeqId idOf(const Envelope& env) const { return env.id(); }

      protected:
         std::shared_ptr<RouterInterface> router_;
         const std::string       name_;
         std::atomic<SeqId>      seqNo_{ 1 };
         SeqId             lastProcessedSeqNo_{ 0 };
         std::set<SeqId>   deferredIds_;
         SeqId currentEnvId_{ 0 };
      };

      class Queue_Locking : public QueueInterface
      {
      public:
         Queue_Locking(const std::shared_ptr<RouterInterface> &
            , const std::shared_ptr<spdlog::logger> &, const std::string& name = {}
            , const std::map<int, std::string> & = {}, bool accounting = true);
         ~Queue_Locking() override;

         void terminate() override;
         void bindAdapter(const std::shared_ptr<Adapter> &) override;
         std::set<UserValue> supportedReceivers() const override;

         bool pushFill(Envelope &) override;

      private:
         void stop();
         void process();

      private:
         std::shared_ptr<spdlog::logger>  logger_;
         const std::map<int, std::string> accMap_;
         const bool accounting_;
         const std::chrono::seconds deferredQueueInterval_{ 30 };
         const std::chrono::seconds accountingInterval_{ 600 };
         std::deque<Envelope>    queue_;
         std::condition_variable cvQueue_;
         std::mutex              cvMutex_;
         std::atomic_bool        running_{ true };
         std::thread             thread_;
      };
      using Queue = Queue_Locking;    // temporary hack to avoid name clashing with ThreadSafeClasses.h


      class Bus
      {
      public:
         virtual ~Bus() = default;

         virtual void addAdapter(const std::shared_ptr<Adapter> &) = 0;

         virtual void SetCommunicationDumpEnabled(bool) {};
      };

   } // namespace message
} // namespace bs

#endif	// MESSAGE_BUS_H
