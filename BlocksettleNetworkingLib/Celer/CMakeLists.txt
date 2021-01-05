#
#
# ***********************************************************************************
# * Copyright (C) 2018 - 2020, BlockSettle AB
# * Distributed under the GNU Affero General Public License (AGPL v3)
# * See LICENSE or http://www.gnu.org/licenses/agpl.html
# *
# **********************************************************************************
#
#
# CMake 3.10 fixes automoc Q_NAMESPACE detection
CMAKE_MINIMUM_REQUIRED( VERSION 3.10 )

PROJECT( CelerLib )

FILE(GLOB SOURCES
   *.cpp
)

FILE(GLOB HEADERS
   *.h
)

INCLUDE_DIRECTORIES( ${CRYPTO_LIB_INCLUDE_DIR} )
INCLUDE_DIRECTORIES( ${WALLET_LIB_INCLUDE_DIR} )
INCLUDE_DIRECTORIES( ${PATH_TO_GENERATED})
INCLUDE_DIRECTORIES( ${Qt5Network_INCLUDE_DIRS} )
INCLUDE_DIRECTORIES( ${Qt5Core_INCLUDE_DIRS})
INCLUDE_DIRECTORIES( ${Qt5Core_INCLUDE_DIRS})
INCLUDE_DIRECTORIES( ${COMMON_LIB_INCLUDE_DIR} )
INCLUDE_DIRECTORIES( ${BS_NETWORK_INCLUDE_DIR} )
INCLUDE_DIRECTORIES( ${BOTAN_INCLUDE_DIR} )

LIST (APPEND SOURCES ${AUTHEID_UTILS})

ADD_LIBRARY(CelerLib ${SOURCES} ${HEADERS})

TARGET_INCLUDE_DIRECTORIES(CelerLib PRIVATE )

TARGET_LINK_LIBRARIES(CelerLib
   ${COMMON_LIB_NAME}
   ${BOTAN_LIB}
   ${LIBCP_LIB}
   ${ZMQ_LIB}
   ${CPP_WALLET_LIB_NAME} 
   ${AUTH_PROTO_LIB_NAME}
   ${BS_PROTO_LIB_NAME}
   ${CELER_PROTO_LIB_NAME}
   ${PROTO_LIB}
   Qt5::Core
   Qt5::Network
   Qt5::Sql
)
