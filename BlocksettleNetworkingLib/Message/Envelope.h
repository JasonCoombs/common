/*

***********************************************************************************
* Copyright (C) 2020 - 2021, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/
#ifndef MESSAGE_ENVELOPE_H
#define MESSAGE_ENVELOPE_H

#include <chrono>
#include <map>
#include <memory>
#include <string>

namespace bs {
   namespace message {
      using UserValue = int;
      using bus_clock = std::chrono::steady_clock;

      static_assert(bus_clock::is_steady, "Should be steady clock");
      static_assert(bus_clock::period::den >= 10000, "Should be at least ms");

      using TimeStamp = std::chrono::time_point<bus_clock>;

      class User
      {
      public:
         virtual ~User() = default;
         User(UserValue value) : value_(value) {}
         UserValue value() const { return value_; }
         template<typename T> T value() const { return static_cast<T>(value_); }

         virtual std::string name() const { return std::to_string(value_); }
         virtual bool isSystem() const { return false; }
         virtual bool isSupervisor() const { return false; }
         virtual bool isBroadcast() const { return false; }
         virtual bool isFallback() const { return false; }

      private:
         UserValue   value_;
      };


      class UserSystem : public User
      {
      public:
         UserSystem() : User(0) {}

         std::string name() const override { return "System"; }
         bool isSystem() const override { return true; }
      };

      class UserSupervisor : public User
      {
      public:
         UserSupervisor() : User(0) {}

         std::string name() const override { return "Supervisor"; }
         bool isSupervisor() const override { return true; }
      };

      class UserFallback : public User
      {
      public:
         UserFallback() : User(0) {}

         std::string name() const override { return "Fallback"; }
         bool isFallback() const override { return true; }
      };


      using SeqId = uint64_t;

      enum class EnvelopeFlags : SeqId
      {
         GlobalBroadcast = UINT64_MAX,
         Publish = UINT64_MAX - 1,           // response to subscription request
         Response = UINT64_MAX - 2,          // response without specific request id - just to signify a non-request
         MinValue = UINT64_MAX - 15          // all values above should be treated as flags only
      };

      struct Envelope
      {
         Envelope() {}
         Envelope(const std::shared_ptr<User>& s, const std::shared_ptr<User>& r
            , const std::string& msg, SeqId respId = 0)
            : sender(s), receiver(r), message(msg), responseId(respId)
         {}
         Envelope(const std::shared_ptr<User>& s, const std::shared_ptr<User>& r
            , const TimeStamp& execAt, const std::string& msg, SeqId respId = 0)
            : sender(s), receiver(r), executeAt(execAt), message(msg)
            , responseId(respId)
         {}

         SeqId id() const { return id_; }
         void setId(SeqId id) { id_ = id; }

         std::shared_ptr<User>   sender;
         std::shared_ptr<User>   receiver;
         TimeStamp   posted;
         TimeStamp   executeAt;
         std::string message;
         SeqId responseId{ 0 };  // should be set in reply and for special flags

      private:
         SeqId id_{ 0 };   // always unique and growing (no 2 envelopes can have the same id)
      };

   } // namespace message
} // namespace bs

#endif	// MESSAGE_ENVELOPE_H
