//NAME: DAQ2CSV.cpp
//SYNOPSIS: ./DAQ2CSV [DAQFILE].DAQ [VECTORNAVFILE].CSV [CONFIGFILE].txt
//COMPILATION: g++ -g -o DAQ2CSV DAQ2CSV.cpp   // hopefully add to make in the near future
//USAGE: Converts a DAQ file a time stamped CSV file. Reads a config file to determine the
//	sample rate and number of DAQ channels being sampled. Reads the first time stamp
//	from the VectorNav, then time stamps each DAQ sample using a calculated period. The
//	corresponding timestamps are calculated using [initTime]+[period]*[iterator].
//		Eg. 5th sample will have timestamp of [initTime] +[period]*5  
//
#include <fstream>
#include <iostream>
#include <stdio.h>
#include <fcntl.h>
#include <cstring>
#include <sys/stat.h>
// Include this header file to get access to VectorNav sensors.
#include "vn/sensors.h"
// We need this file for our sleep function.
#include "vn/thread.h"
#include "vn/compositedata.h"
#include "vn/util.h"

#include "yaml-cpp/yaml.h"

#define HEADERSIZE 20
//#define PERIOD 5000000
#define NAMEMAX 100
using namespace std;
using namespace vn::math;
using namespace vn::sensors;
using namespace vn::protocol::uart;
using namespace vn::xplat;

// Converts [FILENAME].DAQ to [FILENAME].CSV
char * outFileNameConvert(char * inFileName);
unsigned short calculateCRC(unsigned char data[], unsigned int length);

int main (int argc, char * argv[]){
	ofstream outFile;
	double  dblBuffer;
	uint64_t start_time;
	int rate, numChan;
	FILE * DAQFile;
  FILE * VNFile;

  if (argc != 4) {
    cerr << "Usage: ./DAQ2CSV [DAQFile] [VNFile] [ConfigFile]" << endl;
    return -1; 
  }

	DAQFile = fopen(argv[1], "rb");
	VNFile = fopen(argv[2], "rb+");		

	if (VNFile == NULL) {
		perror("Can't open file: ");
	}

  // setup file descriptors for parsing
	char * outFileName = outFileNameConvert(argv[1]);
	outFile.open(outFileName);
	outFile.precision(17);

	int DAQfileDesc = fileno(DAQFile);
	int VNfileDesc = fileno(VNFile);
	int fileSize = lseek(DAQfileDesc, 0, SEEK_END); // Find filesize
	lseek(DAQfileDesc, 0, SEEK_SET); //Reset to the beginning

  YAML::Node config = YAML::LoadFile(argv[3]);
  rate = config["daq"]["rate"].as<int>();

	float floatRate = (float)rate;
	numChan = config["daq"]["chan_num"].as<int>();
	float  period = (((float) 1)/(floatRate))*((float)(1000000000)); // convert rate to period in nanoseconds. VectorNav GPS timestamp is in nanoseconds.
	int intPeriod = (int)period; // convert float period back to int.

	
	// extract first timestamp from VectorNav File
  // read header to get the packet size
	char head_buffer[HEADERSIZE];
	int numRead = read(VNfileDesc, head_buffer, HEADERSIZE);
	if (numRead == -1) {
		perror("Can't read first packet: ");
	}
  size_t pack_size = Packet::computeBinaryPacketLength(head_buffer);
	char buffer[pack_size];
	numRead = read(VNfileDesc, buffer, pack_size - HEADERSIZE);
	numRead = read(VNfileDesc, buffer, pack_size);
	if (numRead == -1) {
		perror("Can't read first packet: ");
	}
	if(numRead == pack_size) {
		Packet p(buffer, pack_size);
		unsigned char check[pack_size - 1];
		memcpy(check, &buffer[1], pack_size - 1);
		if (calculateCRC(check, pack_size - 1) == 0) {
      CompositeData cd = CompositeData::parse(p);
      if (cd.hasTimeGps()) {
        start_time = cd.timeGps();
      } else { 
        perror("Data packets do not have GPS times");
      }
		}
	}

	// write daq results into csv
	cout << "Writing DAQ CSV File." << endl;
  cout << "Number of Samples: " << fileSize/sizeof(double)/numChan << endl;
	for(uint64_t i = 0; i < fileSize/sizeof(double)/numChan; i++){
    // extrapolate time from vectornav time and sample period
    // TODO: once synchronization is complete, vectornav samples should line up with every x number 
    // of DAQ samples. Use this fact to make these timestamps better.
		outFile << (start_time + (intPeriod*i));
		for(int j = 0; j < numChan; j++){
			fread(&dblBuffer , sizeof(double), 1, DAQFile);
			outFile << "," <<  dblBuffer;
		}
		outFile << "\n";
	}	
	fclose(DAQFile);
	outFile.close();
	free(outFileName);
	return 0;
}

char * outFileNameConvert( char * inFileName){
	size_t length = strlen(inFileName);
	char * outFileName = (char*)malloc((size_t)NAMEMAX);
	strncpy(outFileName, inFileName, NAMEMAX);

	//change RAW to CSV
	outFileName[length-3] = 'C';
	outFileName[length-2] = 'S';
	outFileName[length-1] = 'V';

	return outFileName;
}

// Calculates the 16-bit CRC for the given ASCII or binary message.
unsigned short calculateCRC(unsigned char data[], unsigned int length)
{
	unsigned int i;
	unsigned short crc = 0;
	for (i = 0; i<length; i++) {
		crc = (unsigned char)(crc >> 8) | (crc << 8);
		crc ^= data[i];
		crc ^= (unsigned char)(crc & 0xff) >> 4;
		crc ^= crc << 12;
		crc ^= (crc & 0x00ff) << 5;
	}
	return crc;
}
