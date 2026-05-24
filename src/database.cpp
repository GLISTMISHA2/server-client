#include <string>
#include <vector>
#include <libpq-fe.h>
#include <nlohmann/json.hpp>
#include "types.h"

using json = nlohmann::json;

void insertToDatabase(PGconn* db_con, const LocationData& data) {
    if (!db_con || PQstatus(db_con) != CONNECTION_OK) return;
    
    for (const auto& t : data.towers) {
        if (t.type != "LTE") continue;
        
        const char* p[17];
        char b[17][64];
        snprintf(b[0],  64, "%f", data.lat); 
        snprintf(b[1],  64, "%f", data.lon);
        snprintf(b[2],  64, "%f", data.alt); 
        snprintf(b[3],  64, "%f", data.accuracy);
        snprintf(b[4],  64, "%lld", data.timestamp); 
        snprintf(b[5],  64, "%d", t.pci);
        snprintf(b[6],  64, "%d", t.rsrp); 
        snprintf(b[7],  64, "%d", t.rsrq);
        snprintf(b[8],  64, "%d", t.rssnr); 
        snprintf(b[9],  64, "%d", t.mcc);
        snprintf(b[10], 64, "%d", t.mnc); 
        snprintf(b[11], 64, "%d", t.tac);
        snprintf(b[12], 64, "%s", t.type.c_str()); 
        snprintf(b[13], 64, "%d", t.earfcn);
        snprintf(b[14], 64, "%d", t.asu); 
        snprintf(b[15], 64, "%d", t.ci);
        snprintf(b[16], 64, "%d", t.dbm);
        
        for(int i = 0; i < 17; i++) p[i] = b[i];
        
        const char* q = "INSERT INTO mobile_data (lat,lon,alt,accuracy,timestamp,pci,rsrp,rsrq,rssnr,mcc,mnc,tac,type,earfcn,asu,ci,dbm) "
                        "VALUES ($1,$2,$3,$4,$5,$6,$7,$8,$9,$10,$11,$12,$13,$14,$15,$16,$17)";
                        
        PQclear(PQexecParams(db_con, q, 17, NULL, p, NULL, NULL, 0));
    }
}