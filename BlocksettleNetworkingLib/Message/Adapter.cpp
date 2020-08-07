#include "Message/Adapter.h"
#include "Message/Bus.h"

using namespace bs::message;

bool Adapter::push(const Envelope &env)
{
   if (!queue_) {
      return false;
   }
   return queue_->push(env);
}

bool Adapter::pushFill(Envelope &env)
{
   if (!queue_) {
      return false;
   }
   return queue_->pushFill(env);
}


bool PipeAdapter::process(const Envelope &env)
{
   if (!endpoint_) {
      return false;
   }
   return endpoint_->push(env);
}
