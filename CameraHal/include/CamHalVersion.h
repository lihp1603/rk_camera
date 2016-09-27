/***********************************************
v1.0.0:
	release init camera hal version.
v1.1.0: 
	1. fix some ae bugs
	2. fix manual isp bugs
v1.2.0:
	1. support dump RAW file.
v1.3.0:
	1. isp tunning tool function has been verified.
	2. optimize aec algorithm.
	3. disable lots of info logs.
v1.4.0:
	1. ov2710 & ov4689 optimized IQ
v1.5.0:
	1. hst weight and flt setting are not expected,fix it
	2. update ov4689 xml
v1.6.0:
	1. fix get metadata bug
	2. fix mapSensorExpToHal bug
	3. add imx323 tunning file
	4. fix ae bugs
	5. wdr & adpf can config in tunning file 
v1.7.0:
	1. ov2710 & ov4689 WDR and adpf off, GOC on
v1.8.0:
	1. fix calibdb memory leak
	2. CameraBuffer support timestamp
	3. update im3x3 tunning file
v1.9.0:
	1. can set  wb & fliker mode & ae bias 
	   through user interface
	2. ae was stopped for ov4689 under paticular condition,
	   fix it.
v1.a.0:
	1. imx323,ov4689,ov2710 wdr on,goc off
***********************************************/
#define CAMHALVERSION "1.a.0"
