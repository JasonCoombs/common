#
#
# ***********************************************************************************
# * Copyright (C) 2018 - 2021, BlockSettle AB
# * Distributed under the GNU Affero General Public License (AGPL v3)
# * See LICENSE or http://www.gnu.org/licenses/agpl.html
# *
# **********************************************************************************
#
#
# CMake 3.10 fixes automoc Q_NAMESPACE detection
CMAKE_MINIMUM_REQUIRED( VERSION 3.10 )
SET(CMAKE_CXX_STANDARD 17)

PROJECT( ${BS_NETWORK_LIB_NAME} )

FILE(GLOB SOURCES
   *.cpp
   Wallets/*.cpp
   ChatProtocol/*.cpp
   Encryption/*.cpp
   Adapters/*.cpp
   Message/*.cpp
)

IF (NOT DISABLE_CELER)
   ADD_SUBDIRECTORY(Celer)
ELSE ()
   list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/AssetManager.cpp)
   list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/AuthAddressManager.cpp)
   list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/BsClient.cpp)
   list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/BSMarketDataProvider.cpp)
   list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CCFileManager.cpp)
   list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/CCPubConnection.cpp)
   list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/MarketDataProvider.cpp)
   list(REMOVE_ITEM SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/QuoteProvider.cpp)
ENDIF (DISABLE_CELER)


FILE(GLOB HEADERS
   *.h
   Wallets/*.h
   ChatProtocol/*.h
   ChatProtocol/DataObjects/*.h
   ChatProtocol/RequestObjects/*.h
   ChatProtocol/ResponseObjects/*.h
   ChatClientTree/*.h
   Encryption/*.h
   Adapters/*.h
   Message/*.h
)

IF (DISABLE_CELER)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/AssetManager.h)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/AuthAddressManager.h)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/BsClient.h)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/BSMarketDataProvider.h)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/CCFileManager.h)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/CCPubConnection.h)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/CommonTypes.h)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/MarketDataProvider.h)
   list(REMOVE_ITEM HEADERS ${CMAKE_CURRENT_SOURCE_DIR}/QuoteProvider.h)
ENDIF (DISABLE_CELER)


# for bdmenums.h include in ApplicationSettings.h ( ugly :( )
INCLUDE_DIRECTORIES( ${CRYPTO_LIB_INCLUDE_DIR} )
INCLUDE_DIRECTORIES( ${WALLET_LIB_INCLUDE_DIR} )

INCLUDE_DIRECTORIES( ${PATH_TO_GENERATED})

INCLUDE_DIRECTORIES( ${Qt5Network_INCLUDE_DIRS} )
INCLUDE_DIRECTORIES( ${Qt5Core_INCLUDE_DIRS})

INCLUDE_DIRECTORIES( ${Qt5Core_INCLUDE_DIRS})

INCLUDE_DIRECTORIES( ${COMMON_LIB_INCLUDE_DIR} )

INCLUDE_DIRECTORIES( ${BOTAN_INCLUDE_DIR} )

LIST (APPEND SOURCES ${AUTHEID_UTILS})

ADD_LIBRARY( ${BS_NETWORK_LIB_NAME} ${SOURCES} ${HEADERS})

TARGET_INCLUDE_DIRECTORIES(${BS_NETWORK_LIB_NAME} PRIVATE )

#      ${CELER_PROTO_LIB_NAME}
IF (DISABLE_CELER)
   TARGET_LINK_LIBRARIES(${BS_NETWORK_LIB_NAME}
      ${COMMON_LIB_NAME}
      ${BOTAN_LIB}
      ${LIBCP_LIB}
      ${ZMQ_LIB}
      ${CPP_WALLET_LIB_NAME}
      ${AUTH_PROTO_LIB_NAME}
      ${BS_PROTO_LIB_NAME}
      ${PROTO_LIB}
      Qt5::Core
      Qt5::Network
      Qt5::Sql
   )
ELSE ()
   TARGET_LINK_LIBRARIES(${BS_NETWORK_LIB_NAME}
      ${COMMON_LIB_NAME}
      ${BOTAN_LIB}
      ${LIBCP_LIB}
      ${ZMQ_LIB}
      ${CPP_WALLET_LIB_NAME}
      ${AUTH_PROTO_LIB_NAME}
      ${BS_PROTO_LIB_NAME}
      ${PROTO_LIB}
      CelerLib
      Qt5::Core
      Qt5::Network
      Qt5::Sql
   )
ENDIF (DISABLE_CELER)
