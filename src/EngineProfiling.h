#pragma once
#include <containers.hpp>
#include "HookManager.hpp"


class ArmaProf;

class PCounter {
public:
	ArmaProf* boss;
	const char* name;
	const char* cat;
	int slot, stuff2;

	bool shouldTime();
	
};


class PCSlot {
public:
	intercept::types::r_string name;
	intercept::types::r_string category;
	int a,b,c,d,e,f;
	bool g,h,enabled;
};


class ScopeProf {
public:
	int64_t start;
	PCounter* counter;
	bool enabled, other;
	intercept::types::r_string stuffz;

	void doEnd();
};

class ArmaProf {
	friend class PCounter;
public:

    void scopeCompleted(int64_t start, int64_t end, intercept::types::r_string* stuff, PCounter* stuff2);
	void frameEnd(float fps, float time, int smth);

public:
	//This is engine stuff.
	bool da,db,dc;
	int dd,df;
	bool de,dg,dh;
	int ab,dj;
	float stuff;
	int numba;
	PCSlot* stuffzi;
	long suttfu2;


	//
	bool dfddfg;
	char sfsdf[40];
	//


	void* stuffings;
	long stuffonz;
	intercept::types::auto_array<void*> stuffz;
	intercept::types::auto_array<void*> stuffz2;
	intercept::types::auto_array<void*> stuffz3;
	intercept::types::auto_array<void*> stuffz4;

	bool megaOof;
#ifndef __linux__
	int64_t ouf1;
	int64_t ouf2;
#endif
	int64_t ouf3;
	int64_t ouf4;
	int64_t ouf5;
	int64_t ouf6;
	int blios;
	intercept::types::r_string blip;
	float blop;

    float dummy
#ifndef __linux__
	, dummy2, dummy3
#endif
	;//no idea what dis is.. Stuff above is probably wrong somewhere

    bool forceCapture;
    bool capture;
	int stuffiz;

	int64_t framestart;
	int64_t stuffdfdf;


	//
	void* blisdfsd;
	bool lsdfsdf;
	unsigned int sdfsdfsdf;
	//
	intercept::types::r_string blooorp;

};

class EngineProfiling {
public:
	EngineProfiling();
	~EngineProfiling() = default;

    ArmaProf* armaP;
	HookManager hooks;
};