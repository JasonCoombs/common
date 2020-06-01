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
CMAKE_MINIMUM_REQUIRED( VERSION 3.3 )

PROJECT( ${CPP_WALLET_LIB_NAME} )

FILE(GLOB SOURCES *.cpp)
file(GLOB HEADERS *.h)

INCLUDE_DIRECTORIES( ${CRYPTO_LIB_INCLUDE_DIR} )
INCLUDE_DIRECTORIES( ${COMMON_LIB_INCLUDE_DIR} )

INCLUDE_DIRECTORIES( ${BOTAN_INCLUDE_DIR} )

ADD_LIBRARY(${CPP_WALLET_LIB_NAME} ${SOURCES} ${HEADERS})

TARGET_LINK_LIBRARIES(${CPP_WALLET_LIB_NAME}
   ${CRYPTO_LIB_NAME}
   ${LIBBTC_LIB}
   ${MPIR_LIB}
   ${BOTAN_LIB}
   ${COMMON_LIB_NAME}
   ${AUTH_PROTO_LIB_NAME}
   ${BS_PROTO_LIB_NAME}
)
