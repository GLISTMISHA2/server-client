#ifndef DATABASE_H
#define DATABASE_H

#include "types.h"
#include <libpq-fe.h>

int getIntSafely(const json& j, const std::string& key, int d = 0);
CellTowerData parseTower(const json& j, const std::string& type);
void saveToJsonFile(const LocationData& data);
void insertToDatabase(PGconn* db_con, const LocationData& data);

#endif