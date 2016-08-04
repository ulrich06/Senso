#include <EEPROM.h>
#define EEPROM_SIZE 512

const int ADD_BUFFER = 0;
const int ADD_TIMESTAMP = ADD_BUFFER + sizeof(int);
const int ADD_SYNC = ADD_TIMESTAMP + sizeof(long);
const int ADD_NX_SYNC = ADD_SYNC + sizeof(int);
const int ADD_SAMPLING = ADD_NX_SYNC + sizeof(long);
const int ADD_NX_SAMPLING = ADD_SAMPLING + sizeof(int);
const int ADD_BEGIN_DATA = ADD_NX_SAMPLING + sizeof(long);
const int SIZE_UNIT_DATA = sizeof(int) + sizeof(float) + sizeof(long);

void reset();
void setBufferSize(int);
int getBufferSize();
int isBufferFull();
void setSync(int);
void setSampling(int);
void setNxSampling(long);
void setNxSync(long);
void setTimestamp(long);


int pointerData() {
  return ADD_BEGIN_DATA + getBufferSize() * SIZE_UNIT_DATA;
}

void reset() {
  for (int i = 0; i < EEPROM_SIZE; i ++){
    EEPROM.write(i, 0xFF);
  }
  setTimestamp(0L);
  setSync(0);
  setNxSync(0L);
  setSampling(0);
  setNxSync(0L);
  setBufferSize(0);
}

void flushBuffer() {
  for (int i = ADD_BEGIN_DATA; i < EEPROM_SIZE; i ++){
    EEPROM.write(i, 0xFF);
  }
  setBufferSize(0);
}


void print_eeprom() {
  for (int i = 0; i < EEPROM_SIZE; i++){
    Serial.print(EEPROM.read(i), HEX);
    Serial.print(" ");
  }
  Serial.println();
}

void addData(int id, float value, long time){
  if (!isBufferFull()){
    int addr = pointerData();
    EEPROM.put(addr, id);
    EEPROM.put(addr + sizeof(int), value);
    EEPROM.put(addr + sizeof(int) + sizeof(float), time);
    EEPROM.commit();
    setBufferSize(getBufferSize() + 1);
  }
}

void setBufferSize(int size){
  EEPROM.put(ADD_BUFFER, size);
  EEPROM.commit();
}

void setSync(int period){
  EEPROM.put(ADD_SYNC, period);
  EEPROM.commit();
}

void setNxSync(long time){
  EEPROM.put(ADD_NX_SYNC, time);
  EEPROM.commit();
}

void setTimestamp(long timestamp){
  EEPROM.put(ADD_TIMESTAMP, timestamp);
  EEPROM.commit();
}

void setSampling(int period){
  EEPROM.put(ADD_SAMPLING, period);
  EEPROM.commit();
}

void setNxSampling(long time){
  EEPROM.put(ADD_NX_SAMPLING, time);
  EEPROM.commit();
}

int getBufferSize(){
  int i = 0;
  EEPROM.get(ADD_BUFFER, i);
  if (i == -1) return 0; else return i;
}

int getSync(){
  int i = 0;
  EEPROM.get(ADD_SYNC, i);
  if (i == -1) return 0; else return i;
}

long getNxSync(){
  long i = 0L;
  EEPROM.get(ADD_NX_SYNC, i);
  if (i == -1) return 0L; else return i;
}

int getSampling(){
  int i = 0;
  EEPROM.get(ADD_SAMPLING, i);
  if (i == -1) return 0; else return i;
}

long getNxSampling(){
  long i = 0L;
  EEPROM.get(ADD_NX_SAMPLING, i);
  if (i == -1) return 0L; else return i;
}

long getTimestamp(){
  long i = 0L;
  EEPROM.get(ADD_TIMESTAMP, i);
  if (i == -1) return 0L; else return i;
}

int isBufferFull(){
 return getBufferSize() >= MAX_BUFFER;
}

int isBufferEmpty(){
  return getBufferSize() == 0;
}

Smartcampus * getBuffer(){
  const int size = getBufferSize();
  Smartcampus dataArray[size];
  memset(dataArray, 0, sizeof(dataArray));
  for (int i = ADD_BEGIN_DATA; i < pointerData(); i = i + SIZE_UNIT_DATA){
     int id = 0; float v = 0.0; long t = 0L;
     EEPROM.get(i,  id);
     EEPROM.get(i + sizeof(int), v);
     EEPROM.get(i + sizeof(int) + sizeof(float), t);
     dataArray[i/SIZE_UNIT_DATA - 2].n = id;
     dataArray[i/SIZE_UNIT_DATA - 2].v = v;
     dataArray[i/SIZE_UNIT_DATA - 2].t = t;
  }
  return dataArray;
}
