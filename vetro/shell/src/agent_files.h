#pragma once
#include "agent_control.h"
extern "C" {
struct VcData;struct PcData;
VcData* vc_read_file(VcControl*,const char* path);
char* vc_write_file(VcControl*,const char* path,const uint8_t*,size_t);
char* vc_file_path(VcControl*,const char* path,int write);
const uint8_t* vc_data_bytes(const VcData*);
size_t vc_data_size(const VcData*);
void vc_data_free(VcData*);
char* pc_open_data(PcBridge*,const char* path,const uint8_t*,size_t);
PcData* pc_encode_data(PcBridge*,const char* name);
int pc_mark_data_saved(PcBridge*,const char* path);
const uint8_t* pc_data_bytes(const PcData*);
size_t pc_data_size(const PcData*);
char* pc_data_warnings(const PcData*);
void pc_data_free(PcData*);
}
