#ifndef SendMessageRequest_h__
#define SendMessageRequest_h__

#include "Request.h"

namespace Chat {
   class SendMessageRequest : public Request
   {
   public:
      SendMessageRequest(const std::string& clientId
                     , const std::string& messageData);
      QJsonObject toJson() const override;
      static std::shared_ptr<Request> fromJSON(const std::string& clientId
                                     , const std::string& jsonData);
      void handle(RequestHandler &) override;
      const std::string& getMessageData() const;

   private:
      std::string messageData_;
   };
}

#endif // SendMessageRequest_h__
