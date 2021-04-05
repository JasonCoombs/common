/*

***********************************************************************************
* Copyright (C) 2020, BlockSettle AB
* Distributed under the GNU Affero General Public License (AGPL v3)
* See LICENSE or http://www.gnu.org/licenses/agpl.html
*
**********************************************************************************

*/

#ifndef COLORED_COIN_CACHE_H
#define COLORED_COIN_CACHE_H

#include <memory>
#include <string>

struct ColoredCoinSnapshot;
struct ColoredCoinZCSnapshot;

std::string serializeColoredCoinSnapshot(const std::shared_ptr<ColoredCoinSnapshot> &snapshot);
std::string serializeColoredCoinZcSnapshot(const std::shared_ptr<ColoredCoinZCSnapshot> &snapshot);

// Will return empty result in case of errors
std::shared_ptr<ColoredCoinSnapshot> deserializeColoredCoinSnapshot(const std::string &data);
std::shared_ptr<ColoredCoinZCSnapshot> deserializeColoredCoinZcSnapshot(const std::string &data);

#endif
