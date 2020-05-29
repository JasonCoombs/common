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

PROJECT( ${COMMON_LIB_NAME} )

FILE(GLOB SOURCES *.cpp)
file(GLOB HEADERS *.h)

ADD_LIBRARY( ${COMMON_LIB_NAME} ${SOURCES} ${HEADERS})
TARGET_LINK_LIBRARIES(${COMMON_LIB_NAME}
	${CRYPTO_LIB_NAME}
)
