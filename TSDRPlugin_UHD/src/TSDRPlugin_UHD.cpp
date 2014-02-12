#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <uhd/utils/thread_priority.hpp>
#include <uhd/utils/safe_main.hpp>
#include <uhd/usrp/multi_usrp.hpp>
#include <uhd/transport/udp_simple.hpp>
#include <uhd/exception.hpp>
#include <boost/program_options.hpp>
#include <boost/format.hpp>
#include <boost/thread.hpp>
#include <iostream>
#include <complex>

#include "TSDRPlugin.h"
#include "TSDRCodes.h"

#include <stdint.h>
#include <boost/algorithm/string.hpp>

#include "errors.hpp"

uhd::usrp::multi_usrp::sptr usrp;
namespace po = boost::program_options;

uint32_t req_freq = 105e6;
float req_gain = 1;
uint32_t req_rate = 25e6;
volatile int is_running = 0;

EXTERNC void __stdcall tsdrplugin_getName(char * name) {
	uhd::set_thread_priority_safe();
	strcpy(name, "TSDR UHD USRP Compatible Plugin");
}

double tousrpgain(float gain) {
	try {
		uhd::gain_range_t range = usrp->get_rx_gain_range();
		return gain * (range.stop() - range.start()) + range.start();
	}
	catch (std::exception const&  ex)
	{
		return gain * 60;
	}
}

EXTERNC int __stdcall tsdrplugin_init(const char * params) {
	uhd::set_thread_priority_safe();

	// simulate argv and argc
	std::string sparams(params);

	typedef std::vector< std::string > split_vector_type;

	split_vector_type argscounter;
	boost::split( argscounter, sparams, boost::is_any_of(" "), boost::token_compress_on );

	int argc = argscounter.size()+1;
	char * argv[argc];
	char zerothtarg[] = "TSDRPlugin_UHD";
	argv[0] = zerothtarg;
	for (int i = 0; i < argc-1; i++)
		argv[i+1] = (char *) argscounter[i].c_str();

	//variables to be set by po
	std::string args, file, ant, subdev, ref;
	double bw, rate;

	//setup the program options
	po::options_description desc("Allowed options");
	desc.add_options()
			("args", po::value<std::string>(&args)->default_value(""), "multi uhd device address args")
			("ant", po::value<std::string>(&ant), "daughterboard antenna selection")
			("rate", po::value<double>(&rate)->default_value(req_rate), "rate of incoming samples")
			("subdev", po::value<std::string>(&subdev), "daughterboard subdevice specification")
			("bw", po::value<double>(&bw), "daughterboard IF filter bandwidth in Hz")
			("ref", po::value<std::string>(&ref)->default_value("internal"), "waveform type (internal, external, mimo)") ;

	po::variables_map vm;
	try {
		po::store(po::parse_command_line(argc, argv, desc), vm);
		po::notify(vm);
	} catch (std::exception const&  ex)
	{
		std::string msg(boost::str(boost::format("Error: %s\n\nTSDRPlugin_UHD %s") % ex.what() % desc));
		RETURN_EXCEPTION(msg.c_str(), TSDR_PLUGIN_PARAMETERS_WRONG);
	}

	try {
		//create a usrp device
		usrp = uhd::usrp::multi_usrp::make(args);

		//Lock mboard clocks
		usrp->set_clock_source(ref);

		usrp->set_rx_rate(rate);
		req_rate = usrp->get_rx_rate();

		//set the rx center frequency
		usrp->set_rx_freq(req_freq);

		//set the rx rf gain
		usrp->set_rx_gain(tousrpgain(req_gain));
		//set the antenna
		if (vm.count("ant")) usrp->set_rx_antenna(ant);

		//set the IF filter bandwidth
		if (vm.count("bw")){
			usrp->set_rx_bandwidth(bw);
			std::cout << boost::format("Actual RX Bandwidth: %f MHz...") % usrp->get_rx_bandwidth() << std::endl << std::endl;
		}

		boost::this_thread::sleep(boost::posix_time::seconds(1)); //allow for some setup time

		//Check Ref and LO Lock detect
		std::vector<std::string> sensor_names;
		sensor_names = usrp->get_rx_sensor_names(0);
		if (std::find(sensor_names.begin(), sensor_names.end(), "lo_locked") != sensor_names.end()) {
			uhd::sensor_value_t lo_locked = usrp->get_rx_sensor("lo_locked",0);
			UHD_ASSERT_THROW(lo_locked.to_bool());
		}
		sensor_names = usrp->get_mboard_sensor_names(0);
		if ((ref == "mimo") and (std::find(sensor_names.begin(), sensor_names.end(), "mimo_locked") != sensor_names.end())) {
			uhd::sensor_value_t mimo_locked = usrp->get_mboard_sensor("mimo_locked",0);
			UHD_ASSERT_THROW(mimo_locked.to_bool());
		}
		if ((ref == "external") and (std::find(sensor_names.begin(), sensor_names.end(), "ref_locked") != sensor_names.end())) {
			uhd::sensor_value_t ref_locked = usrp->get_mboard_sensor("ref_locked",0);
			UHD_ASSERT_THROW(ref_locked.to_bool());
		}
	} catch (std::exception const&  ex)
	{
		RETURN_EXCEPTION(ex.what(), TSDR_CANNOT_OPEN_DEVICE);
	}

	RETURN_OK();
}

EXTERNC uint32_t __stdcall tsdrplugin_setsamplerate(uint32_t rate) {
	req_rate = rate;

	try {
		usrp->set_rx_rate(req_rate);
		double real_rate = usrp->get_rx_rate();
		req_rate = real_rate;
		std::cout << boost::format("Actual RX Rate: %f Msps...") % (real_rate/1e6) << std::endl << std::endl;
	}
	catch (std::exception const&  ex)
	{
	}

	return req_rate;
}

EXTERNC uint32_t __stdcall tsdrplugin_getsamplerate() {
	uhd::set_thread_priority_safe();

	try {
		req_rate = usrp->get_rx_rate();
	}
	catch (std::exception const&  ex)
	{
	}

	return req_rate;
}

EXTERNC int __stdcall tsdrplugin_setbasefreq(uint32_t freq) {
	req_freq = freq;

	try {
		usrp->set_rx_freq(req_freq);
	}
	catch (std::exception const&  ex)
	{
	}

	RETURN_OK();
}

EXTERNC int __stdcall tsdrplugin_stop(void) {
	is_running = 0;
	RETURN_OK();
}

EXTERNC int __stdcall tsdrplugin_setgain(float gain) {
	uhd::set_thread_priority_safe();
	req_gain = gain;
	try {
		usrp->set_rx_gain(tousrpgain(req_gain));
		std::cout << boost::format("Actual RX Gain: %f dB...") % usrp->get_rx_gain() << std::endl << std::endl;
	}
	catch (std::exception const&  ex)
	{
	}
	RETURN_OK();
}

EXTERNC int __stdcall tsdrplugin_readasync(tsdrplugin_readasync_function cb, void *ctx) {
	uhd::set_thread_priority_safe();

	is_running = 1;

	size_t native_buffer_size = 1;
	float * native_buffer = (float *) malloc(sizeof(float) * native_buffer_size);

	try {
		//set the rx sample rate
		usrp->set_rx_rate(req_rate);
		std::cout << boost::format("Actual RX Rate: %f Msps...") % (usrp->get_rx_rate()/1e6) << std::endl << std::endl;

		//set the rx center frequency
		usrp->set_rx_freq(req_freq);

		//set the rx rf gain
		usrp->set_rx_gain(tousrpgain(req_gain));

		//reset timestamps
		// TODO set_time_next?
		usrp->set_time_now(uhd::time_spec_t(0.0));

		//create a receive streamer
		uhd::stream_args_t stream_args("fc32"); //complex floats
		uhd::rx_streamer::sptr rx_stream = usrp->get_rx_stream(stream_args);

		//loop until total number of samples reached
		uhd::rx_metadata_t md;
		std::vector<std::complex<float> > buff(rx_stream->get_max_num_samps());

		//setup streaming
		rx_stream->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_START_CONTINUOUS);

		while(is_running){
			size_t num_rx_samps = rx_stream->recv(
					&buff.front(), buff.size(), md
			);

			//handle the error codes
			switch(md.error_code){
			case uhd::rx_metadata_t::ERROR_CODE_NONE:
				break;

			case uhd::rx_metadata_t::ERROR_CODE_TIMEOUT:
				std::cout << boost::format(
						"Got timeout before all samples received, possible packet loss."
				) << std::endl;
				break;
			case uhd::rx_metadata_t::ERROR_CODE_OVERFLOW:
				// overflow :(
				break;

			default:
				std::cout << boost::format(
						"Got error code 0x%x, exiting loop..."
				) % md.error_code << std::endl;
				goto done_loop;
			}


//			std::cout << boost::format(
//			            "Received packet: %u samples, %u full secs, %f frac secs"
//			        ) % num_rx_samps % md.time_spec.get_full_secs() % md.time_spec.get_frac_secs() << std::endl;

			const size_t items = num_rx_samps*2;
			if (items > native_buffer_size) {
				printf("Resizing\n");fflush(stdout);
				native_buffer_size = items;
				native_buffer = (float *) realloc((void *) native_buffer, sizeof(float) * native_buffer_size);
			}

			float * nativeref = native_buffer;
			for (size_t i = 0; i < num_rx_samps; i++) {
				std::complex<float> sample = buff[i];
				*(nativeref++) = sample.imag();
				*(nativeref++) = sample.real();
			}

			cb(native_buffer, items, ctx, 0);

		} done_loop:

		usrp->issue_stream_cmd(uhd::stream_cmd_t::STREAM_MODE_STOP_CONTINUOUS);

		// flush usrpbuffer
	    while(rx_stream->recv(
	        &buff.front(), buff.size(), md,
	        uhd::device::RECV_MODE_ONE_PACKET
	    )){
	        /* NOP */
	    };
	}
	catch (std::exception const&  ex)
	{
		is_running = 0;
		free(native_buffer);
		RETURN_EXCEPTION(ex.what(), TSDR_CANNOT_OPEN_DEVICE);
	}
	free(native_buffer);
	RETURN_OK();
}

EXTERNC void __stdcall tsdrplugin_cleanup(void) {
	uhd::set_thread_priority_safe();

	try {
		usrp.reset();
		boost::this_thread::sleep(boost::posix_time::seconds(1));
	} catch (std::exception const&  ex) {

	}

	is_running = 0;
}