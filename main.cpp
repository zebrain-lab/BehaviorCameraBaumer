#include <stdlib.h>
#include <stdio.h>
#include <conio.h>
#include <iostream>
#include <fstream>
#include <math.h>
#include <iterator>
#include <string>
#include <ctime>
#include <sstream>
#include <iomanip>
#include <inttypes.h>
using namespace std;

// Baumer SDK : camera SDK
#include "bgapi.hpp"

// OPENCV : display preview and save video 
#include <opencv2/core.hpp>
#include <opencv2/videoio.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>

// Boost : parse config file / create thread for preview
#include "boost/program_options.hpp"
#include "boost/filesystem.hpp"
#include "boost/thread.hpp"
namespace po = boost::program_options;
namespace fs = boost::filesystem;

#define NUMBUFFER 100
#define EXPOSUREMAX 100000
#define FPSMAX 500

// PARAMETERS-------------------------------------------------------------------
int preview;				
int subsample;
string result_dir;			

// Global variables-------------------------------------------------------------
int sys = 0;
int cam = 0;
BGAPI::System * pSystem = NULL;
BGAPI::Camera * pCamera = NULL;
BGAPI::Image * pImage[NUMBUFFER];
cv::VideoWriter writer;
std::ofstream file;
BGAPIX_TypeINT iTimeHigh, iTimeLow, iFreqHigh, iFreqLow;
BGAPIX_CameraImageFormat cformat; 
cv::Mat img_display;
uint64_t first_ts = 0;
uint64_t current_ts = 0;
uint64_t previous_ts = 0;
int cropleft = 0;	            
int croptop = 0;		       
int height = 100;		            
int width = 100;			
int gainvalue = 0;
int gainmax = 0;
int exposurevalue = 0;
int exposuremax = 0;
int subsamplemax = 4;
int triggers = 0;
int fps = 0;
int fpsmax = 0;
int formatindex = 0;
int formatindexmax = 0;

int read_config(int ac, char* av[]) {
	try {
		string config_file;

		// only on command line
		po::options_description generic("Generic options");
		generic.add_options()
			("help", "produce help message")
			("config,c", po::value<string>(&config_file)->default_value("behavior.cfg"),
				"configuration file")
			;

		// both on command line and config file
		po::options_description config("Configuration");
		config.add_options()
			("preview,p", po::value<int>(&preview)->default_value(1), "preview")
			("subsample,b", po::value<int>(&subsample)->default_value(1), "subsampling factor")
			("result_dir", po::value<string>(&result_dir)->default_value(""), "result directory")
			;

		po::options_description cmdline_options;
		cmdline_options.add(generic).add(config);

		po::options_description config_file_options;
		config_file_options.add(config);

		po::options_description visible("Allowed options");
		visible.add(generic).add(config);

		po::variables_map vm;
		store(po::command_line_parser(ac, av).options(cmdline_options).run(), vm);
		notify(vm);

		ifstream ifs(config_file.c_str());
		if (!ifs) {
			cout << "can not open config file: " << config_file << "\n";
			return 1;
		}
		else {
			store(parse_config_file(ifs, config_file_options), vm);
			notify(vm);
		}

		if (vm.count("help")) {
			cout << visible << "\n";
			return 2;
		}

		cout << "Running with following options " << endl
			<< "  Preview: " << preview << endl
			<< "  Subsample: " << subsample << endl
			<< "  Result directory: " << result_dir << endl;
			
	}
	catch (exception& e)
	{
		cout << e.what() << "\n";
		return 1;
	}
	return 0;
}
    
BGAPI_RESULT BGAPI_CALLBACK imageCallback(void * callBackOwner, BGAPI::Image* pCurrImage)
{
	cv::Mat img;
	cv::Mat img_resized;
	int swc;
	int hwc;
	int timestamplow = 0;
	int timestamphigh = 0;
	uint32_t timestamplow_u = 0;
	uint32_t timestamphigh_u = 0;
	BGAPI_RESULT res = BGAPI_RESULT_OK;

	unsigned char* imagebuffer = NULL;
	res = pCurrImage->get(&imagebuffer);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Image::get Errorcode: %d\n", res);
		return 0;
	}

	res = pCurrImage->getNumber(&swc, &hwc);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Image::getNumber Errorcode: %d\n", res);
		return 0;
	}

	res = pCurrImage->getTimeStamp(&timestamphigh, &timestamplow);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Image::getTimeStamp Errorcode: %d\n", res);
		return 0;
	}
	timestamplow_u = timestamplow;
	timestamphigh_u = timestamphigh;
	if (swc == 0) {
		first_ts = (uint64_t) timestamphigh_u << 32 | timestamplow_u;
	}
	current_ts = (uint64_t) timestamphigh_u << 32 | timestamplow_u;
	double current_time = (double)(current_ts - first_ts) / (double)iFreqLow.current;
	double fps_hat = (double)iFreqLow.current / (double)(current_ts - previous_ts);
	previous_ts = current_ts;

	img = cv::Mat(cv::Size(subsample*width, subsample*height), CV_8U, imagebuffer);
	cv::resize(img, img_resized, cv::Size(), 1.0/subsample, 1.0/subsample);

	if (!preview) {
		file << setprecision(3) << std::fixed << 1000 * current_time << std::endl;
		writer << img_resized;
	}

	img_resized.copyTo(img_display);
	cv::putText(img_display, to_string(swc), cv::Point(10, 30), CV_FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 0, 0), 2,cv::LINE_AA, false);
	cv::putText(img_display, to_string(fps_hat), cv::Point(width - 100, 30), CV_FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 0, 0), 2, cv::LINE_AA, false);
	cv::putText(img_display, to_string(current_time) + " s", cv::Point(10, height - 10), CV_FONT_HERSHEY_SIMPLEX, 1, cv::Scalar(255, 0, 0), 2, cv::LINE_AA, false);

	res = ((BGAPI::Camera*)callBackOwner)->setImage(pCurrImage);
	if (res != BGAPI_RESULT_OK) {
		printf("setImage failed with %d\n", res);
	}
	return res;
}

static void trackbar_callback(int,void*) {

	BGAPI_RESULT res = BGAPI_RESULT_FAIL;
	BGAPI_FeatureState state; 
	BGAPIX_TypeROI roi;
	BGAPIX_TypeRangeFLOAT gain;
	BGAPIX_TypeRangeINT exposure;
	BGAPIX_TypeRangeFLOAT framerate;
	BGAPIX_TypeListINT imageformat;

	state.cbSize = sizeof(BGAPI_FeatureState);
	roi.cbSize = sizeof(BGAPIX_TypeROI);
	gain.cbSize = sizeof(BGAPIX_TypeRangeFLOAT);
	exposure.cbSize = sizeof(BGAPIX_TypeRangeINT);
	framerate.cbSize = sizeof(BGAPIX_TypeRangeFLOAT);
	imageformat.cbSize = sizeof(BGAPIX_TypeListINT);

	// ROI
	res = pCamera->setPartialScan(1, cropleft, croptop, cropleft + subsample * width, croptop + subsample * height);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setPartialScan Errorcode: %d\n", res);
	}

	res = pCamera->getPartialScan(&state, &roi);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::getImageFormat Errorcode: %d\n", res);
	}

	cropleft = roi.curleft;
	croptop = roi.curtop;
	width = roi.curright - roi.curleft;
	height = roi.curbottom - roi.curtop;

	// GAIN
	res = pCamera->setGain(gainvalue);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setGain Errorcode: %d\n", res);
	}

	res = pCamera->getGain(&state, &gain);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setGain Errorcode: %d\n", res);
	}
	gainvalue = gain.current;

	// EXPOSURE 
	res = pCamera->setExposure(exposurevalue);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setExposure Errorcode: %d\n", res);
	}

	res = pCamera->getExposure(&state, &exposure);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setExposure Errorcode: %d\n", res);
	}
	exposurevalue = exposure.current;

	// TRIGGERS
	if (triggers) {
		res = pCamera->setTriggerSource(BGAPI_TRIGGERSOURCE_HARDWARE1);
		if (res != BGAPI_RESULT_OK)
		{
			printf("BGAPI::Camera::setTriggerSource Errorcode: %d\n", res);
		}

		res = pCamera->setTrigger(true);
		if (res != BGAPI_RESULT_OK)
		{
			printf("BGAPI::Camera::setTrigger Errorcode: %d\n", res);
		}

		res = pCamera->setTriggerActivation(BGAPI_ACTIVATION_RISINGEDGE);
		if (res != BGAPI_RESULT_OK)
		{
			printf("BGAPI::Camera::setTriggerActivation Errorcode: %d\n", res);
		}

		res = pCamera->setTriggerDelay(0);
		if (res != BGAPI_RESULT_OK)
		{
			printf("BGAPI::Camera::setTriggerDelay Errorcode: %d\n", res);
		}
	}
	else {
		res = pCamera->setTriggerSource(BGAPI_TRIGGERSOURCE_SOFTWARE);
		if (res != BGAPI_RESULT_OK)
		{
			printf("BGAPI::Camera::setTriggerSource Errorcode: %d\n", res);
		}

		res = pCamera->setTrigger(false);
		if (res != BGAPI_RESULT_OK)
		{
			printf("BGAPI::Camera::setTrigger Errorcode: %d\n", res);
		}
	}
	res = pCamera->getTrigger(&state);
	if (res != BGAPI_RESULT_OK)
	{
		printf("BGAPI::Camera::getTrigger Errorcode: %d\n", res);
	}
	triggers = state.bIsEnabled;
	
	// FPS
	res = pCamera->setFramesPerSecondsContinuous(fps);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setFramesPerSecondsContinuous Errorcode: %d\n", res);
	}

	res = pCamera->getFramesPerSecondsContinuous(&state, &framerate);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::getFramesPerSecondsContinuous Errorcode: %d\n", res);
	}
	fps = framerate.current;

	// FORMAT INDEX
	res = pCamera->setImageFormat(formatindex);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setImageFormat Errorcode: %d\n", res);
	}

	res = pCamera->getImageFormat(&state,&imageformat);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setImageFormat Errorcode: %d\n", res);
	}
	formatindex = imageformat.current;

	res = pCamera->getImageFormatDescription(formatindex, &cformat);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::getImageFormatDescription Errorcode: %d\n", res);
	}
	cv::setTrackbarMax("ROI height", "Controls", cformat.iSizeY);
	cv::setTrackbarMax("ROI width", "Controls", cformat.iSizeX);
	cv::setTrackbarMax("ROI left", "Controls", cformat.iSizeX);
	cv::setTrackbarMax("ROI top", "Controls", cformat.iSizeY);
}

void display_preview() {

	cv::namedWindow("Preview", cv::WINDOW_AUTOSIZE);
	if (preview) {
		cv::namedWindow("Controls", cv::WINDOW_NORMAL);
		cv::createTrackbar("ROI left", "Controls", &cropleft, cformat.iSizeX, trackbar_callback);
		cv::createTrackbar("ROI top", "Controls", &croptop, cformat.iSizeY, trackbar_callback);
		cv::createTrackbar("ROI width", "Controls", &width, cformat.iSizeX, trackbar_callback);
		cv::createTrackbar("ROI height", "Controls", &height, cformat.iSizeY, trackbar_callback);
		cv::createTrackbar("Exposure", "Controls", &exposurevalue, EXPOSUREMAX, trackbar_callback);
		cv::createTrackbar("Gain", "Controls", &gainvalue, gainmax, trackbar_callback);
		cv::createTrackbar("FPS", "Controls", &fps, FPSMAX, trackbar_callback);
		cv::createTrackbar("Triggers", "Controls", &triggers, 1, trackbar_callback);
		cv::createTrackbar("Format Index", "Controls", &formatindex, formatindexmax, trackbar_callback);
	}
	while (true) {
		cv::imshow("Preview", img_display);
		cv::waitKey(16);
	}
}

int setup_camera() {
	
	BGAPI_RESULT res = BGAPI_RESULT_FAIL;
	BGAPI_FeatureState state; 
	BGAPIX_TypeROI roi; 
	BGAPIX_TypeRangeFLOAT gain;
	BGAPIX_TypeRangeFLOAT framerate;
	BGAPIX_TypeRangeINT exposure;
	BGAPIX_TypeListINT imageformat;

	cformat.cbSize = sizeof(BGAPIX_CameraImageFormat);
	state.cbSize = sizeof(BGAPI_FeatureState);
	iTimeHigh.cbSize = sizeof(BGAPIX_TypeINT);
	iTimeLow.cbSize = sizeof(BGAPIX_TypeINT);
	iFreqHigh.cbSize = sizeof(BGAPIX_TypeINT);
	iFreqLow.cbSize = sizeof(BGAPIX_TypeINT);
	roi.cbSize = sizeof(BGAPIX_TypeROI);
	gain.cbSize = sizeof(BGAPIX_TypeRangeFLOAT);
	exposure.cbSize = sizeof(BGAPIX_TypeRangeINT);
	framerate.cbSize = sizeof(BGAPIX_TypeRangeFLOAT);
	imageformat.cbSize = sizeof(BGAPIX_TypeListINT);

	// Initializing the system--------------------------------------------------
	res = BGAPI::createSystem(sys, &pSystem);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::createSystem Errorcode: %d System index: %d\n", res, sys);
		return EXIT_FAILURE;
	}
	printf("Created system: System index %d\n", sys);

	res = pSystem->open();
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::System::open Errorcode: %d System index: %d\n", res, sys);
		return EXIT_FAILURE;
	}
	printf("System opened: System index %d\n", sys);

	res = pSystem->createCamera(cam, &pCamera);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::System::createCamera Errorcode: %d Camera index: %d\n", res, cam);
		return EXIT_FAILURE;
	}
	printf("Created camera: Camera index %d\n", cam);

	res = pCamera->open();
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::open Errorcode: %d Camera index: %d\n", res, cam);
		return EXIT_FAILURE;
	}
	printf("Camera opened: Camera index %d\n", cam);

	res = pCamera->setDataAccessMode(BGAPI_DATAACCESSMODE_QUEUEDINTERN, NUMBUFFER);
	if (res != BGAPI_RESULT_OK)
	{
		printf("BGAPI::Camera::setDataAccessMode Errorcode %d\n", res);
		return EXIT_FAILURE;
	}

	int i = 0;
	for (i = 0; i < NUMBUFFER; i++)
	{
		res = BGAPI::createImage(&pImage[i]);
		if (res != BGAPI_RESULT_OK)
		{
			printf("BGAPI::createImage for Image %d Errorcode %d\n", i, res);
			break;
		}
	}
	printf("Images created successful!\n");

	for (i = 0; i < NUMBUFFER; i++)
	{
		res = pCamera->setImage(pImage[i]);
		if (res != BGAPI_RESULT_OK)
		{
			printf("BGAPI::System::setImage for Image %d Errorcode %d\n", i, res);
			break;
		}
	}
	printf("Images allocated successful!\n");

	res = pCamera->registerNotifyCallback(pCamera, (BGAPI::BGAPI_NOTIFY_CALLBACK) &imageCallback);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::registerNotifyCallback Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}

	res = pCamera->getImageFormat(&state, &imageformat);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setImageFormat Errorcode: %d\n", res);
	}
	formatindex = imageformat.current;
	formatindexmax = imageformat.length;

	res = pCamera->getImageFormatDescription(formatindex, &cformat);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::getImageFormatDescription Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}

	res = pCamera->getGain(&state,&gain);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setGain Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}
	gainvalue = gain.current;
	gainmax = gain.maximum;

	res = pCamera->getExposure(&state,&exposure);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setExposure Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}
	exposurevalue= exposure.current;
	exposuremax = exposure.maximum;

	res = pCamera->getPartialScan(&state, &roi);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::getImageFormat Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}
	cropleft = roi.curleft;
	croptop = roi.curtop;
	width = roi.curright - roi.curleft;
	height = roi.curbottom - roi.curtop;

	res = pCamera->getFramesPerSecondsContinuous(&state, &framerate);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::getFramesPerSecondsContinuous Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}
	fps = framerate.current;
	fpsmax = framerate.maximum;

	res = pCamera->getTrigger(&state);
	if (res != BGAPI_RESULT_OK)
	{
		printf("BGAPI::Camera::getTrigger Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}
	triggers = state.bIsEnabled;

	res = pCamera->setReadoutMode(BGAPI_READOUTMODE_OVERLAPPED);
	if (res != BGAPI_RESULT_OK)
	{
		if (res == BGAPI_RESULT_FEATURE_NOTIMPLEMENTED) {
			printf("BGAPI::Camera::setReadoutMode not implemented, ignoring\n");
		}
		else {
			printf("BGAPI::Camera::setReadoutMode Errorcode: %d\n", res);
			return EXIT_FAILURE;
		}
	}

	res = pCamera->setSensorDigitizationTaps(BGAPI_SENSORDIGITIZATIONTAPS_SIXTEEN);
	if (res != BGAPI_RESULT_OK)
	{
		if (res == BGAPI_RESULT_FEATURE_NOTIMPLEMENTED) {
			printf("BGAPI::Camera::setSensorDigitizationTaps not implemented, ignoring\n");
		}
		else {
			printf("BGAPI::Camera::setSensorDigitizationTaps Errorcode: %d\n", res);
			return EXIT_FAILURE;
		}
	}

	res = pCamera->setExposureMode(BGAPI_EXPOSUREMODE_TIMED);
	if (res != BGAPI_RESULT_OK)
	{
		if (res == BGAPI_RESULT_FEATURE_NOTIMPLEMENTED) {
			printf("BGAPI::Camera::setExposureMode not implemented, ignoring\n");
		}
		else {
			printf("BGAPI::Camera::setExposureMode Errorcode: %d\n", res);
			return EXIT_FAILURE;
		}
	}

	res = pCamera->getTimeStamp(&state, &iTimeHigh, &iTimeLow, &iFreqHigh, &iFreqLow);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::getTimeStamp Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}
	printf("Timestamps frequency [%d,%d]\n", iFreqHigh.current, iFreqLow.current);

	res = pCamera->setFrameCounter(0, 0);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::setFrameCounter Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
}

int run_camera()
{
	BGAPI_RESULT res = BGAPI_RESULT_FAIL;
	BGAPI_FeatureState state; state.cbSize = sizeof(BGAPI_FeatureState);
	BGAPIX_CameraStatistic statistics; statistics.cbSize = sizeof(BGAPIX_CameraStatistic);

    res = pCamera->setStart(true);
    if(res != BGAPI_RESULT_OK) {
        printf("BGAPI::Camera::setStart Errorcode: %d\n",res);
		return EXIT_FAILURE;
    }
	printf("Acquisition started\n");
    
    printf("\n\n=== ENTER TO STOP ===\n\n");
	int d;
	scanf("&d",&d);
    while ((d = getchar()) != '\n' && d != EOF)

    res = pCamera->setStart(false);
    if(res != BGAPI_RESULT_OK) {
        printf("BGAPI::Camera::setStart Errorcode: %d\n",res);
		return EXIT_FAILURE;
    }

	res = pCamera->getStatistic(&state, &statistics);
	if (res != BGAPI_RESULT_OK) {
		printf("BGAPI::Camera::getStatistic Errorcode: %d\n", res);
		return EXIT_FAILURE;
	}
	cout << endl << "Camera statistics:" << endl
		<< "  Received Frames Good: " << statistics.statistic[0] << endl
		<< "  Received Frames Corrupted: " << statistics.statistic[1] << endl
		<< "  Lost Frames: " << statistics.statistic[2] << endl
		<< "  Resend Requests: " << statistics.statistic[3] << endl
		<< "  Resend Packets: " << statistics.statistic[4] << endl
		<< "  Lost Packets: " << statistics.statistic[5] << endl
		<< "  Bandwidth: " << statistics.statistic[6] << endl 
		<< endl;		

	// release all resources ?

    res = pSystem->release();
    if(res != BGAPI_RESULT_OK) {
        printf( "BGAPI::System::release Errorcode: %d System index: %d\n", res,sys);
        return EXIT_FAILURE;
    }
    printf("System released: System index %d\n", sys);

	return EXIT_SUCCESS;
}

int exit_gracefully(int exitcode) {

	printf("\n\n=== ENTER TO CLOSE ===\n\n");
	scanf("&d");

	// Stop the program and release resources 
	if (!preview) {
		file.close();
	}

	cv::destroyAllWindows();
	return exitcode;
}

int main(int ac, char* av[])
{
	int retcode = 0;

	// read configuration files
	int read = 1;
	read = read_config(ac, av);
	if (read == 1) {
		printf("Problem parsing options, aborting");
		return exit_gracefully(1);
	}
	else if (read == 2) {
		return exit_gracefully(0);
	}

	retcode = setup_camera();
	if (retcode == EXIT_FAILURE) {
		return exit_gracefully(EXIT_FAILURE);
	}
	printf("Camera setup complete\n");

	if (!preview) {
		// Check if result directory exists
		fs::path dir(result_dir);
		if (!exists(dir)) {
			if (!fs::create_directory(dir)) {
				cout << "unable to create result directory, aborting" << endl;
				return exit_gracefully(1);
			}
		}

		// Get formated time string 
		time_t now;
		struct tm* timeinfo;
		char buffer[100];
		time(&now);
		timeinfo = localtime(&now);
		strftime(buffer, sizeof(buffer), "%Y_%m_%d_", timeinfo);
		string timestr(buffer);

		// Check if video file exists
		fs::path video;
		stringstream ss;
		int i = 0;
		do {
			ss << setfill('0') << setw(2) << i;
			video = dir / (timestr + ss.str() + ".avi");
			i++;
			ss.str("");
		} while (exists(video));
		char videoname[100];
		wcstombs(videoname, video.c_str(), 100);

		// Check if timestamps file exists
		fs::path ts = video.replace_extension("txt");
		if (exists(ts)) {
			printf("timestamp file exists already, aborting\n");
			return exit_gracefully(1);
		}

		writer.open(videoname, cv::VideoWriter::fourcc('X', '2', '6', '4'), 24, cv::Size(width, height), 0);
		if (!writer.isOpened()) {
			printf("Problem opening Video writer, aborting\n");
			return exit_gracefully(1);
		}

		// Create timestamps file
		file.open(ts.string());
	}
    
	img_display = cv::Mat(subsample*height, subsample*width, CV_8UC3);
	boost::thread bt(display_preview);

	// launch acquisition 
	retcode = run_camera();
	if (retcode == EXIT_FAILURE) {
		return exit_gracefully(EXIT_FAILURE);
	}

	bt.interrupt();

	// Stop the program 
	return exit_gracefully(EXIT_SUCCESS);
}
