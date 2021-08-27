#include <switch.h>
#include <unistd.h>
#include <string>

#include <brynet/base/AppStatus.hpp>
#include <brynet/net/http/HttpFormat.hpp>
#include <brynet/net/http/HttpService.hpp>
#include <brynet/net/http/WebSocketFormat.hpp>
#include <brynet/net/wrapper/HttpServiceBuilder.hpp>
#include <brynet/net/wrapper/ServiceBuilder.hpp>
#include "json/json.hpp"

#include <condition_variable>
#include <iostream>
#include <string>
#include <thread>

using namespace brynet;
using namespace brynet::net;
using namespace brynet::net::http;

using json = nlohmann::json;

//处理http服务对接，接收从远端报来的json数据。
//add by freeeyes

/*
SWITCH_MODULE_RUNTIME_FUNCTION(mod_example_runtime);
*/

SWITCH_MODULE_LOAD_FUNCTION(mod_http_server_load);
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_http_server_shutdown);
SWITCH_MODULE_DEFINITION(mod_http_server, mod_http_server_load, mod_http_server_shutdown,  NULL);

//解析lua对应的字符串,并执行
bool run_lua_file(const std::string lua_json_command, std::string lua_json_return)
{
	switch_stream_handle_t stream = { 0 };
    try
    {
		auto json_value = json::parse(lua_json_command);

		std::string lua_file = json_value["lua file"];
		if("" == lua_file)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[run_lua_file]lua file is null!\n");
			return false;
		}

		std::string lua_file_param = json_value["param"];
		if("" == lua_file_param)
		{
			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[run_lua_file]param is null!\n");
			return false;
		}

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[run_lua_file]lua file=%s!\n", lua_file.c_str());
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[run_lua_file]lua_file_param=%s!\n", lua_file_param.c_str());

		std::string freeswitch_command = lua_file + " " + lua_file_param;

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[run_lua_file]freeswitch_command=%s!\n", freeswitch_command.c_str());

		SWITCH_STANDARD_STREAM(stream);
		
		//luarun是开一个新线程执行
		//switch_api_execute("luarun", freeswitch_command.c_str(), nullptr, &stream);
		switch_api_execute("lua", freeswitch_command.c_str(), nullptr, &stream);		

		//获得返回值
		lua_json_return = (const char*)stream.data;
		switch_safe_free(stream.data);
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[run_lua_file]lua_json_return=%s!\n", lua_json_return.c_str());
		return true;

	}
	catch (const json::parse_error& e)
    {
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[run_lua_file]command error=%s!\n", e.what());
        return false;
    }	

	return true;
}

// cmd为参数列表
// sessin为当前callleg的session
// stream为当前输出流。如果想在Freeswitch控制台中输出什么，可以往这个流里写
#define SWITCH_STANDARD_API(name)  \
static switch_status_t name (_In_opt_z_ const char *cmd, _In_opt_ switch_core_session_t *session, _In_ switch_stream_handle_t *stream)

//全局变量
//配置文件信息
class Chttp_server_config
{
public:
	std::string http_server_addr_ = "";
	std::string http_server_port_ = "";	

	void print_info()
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[Chttp_server_config]http_server_addr_=%s!\n", http_server_addr_.c_str());
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[Chttp_server_config]http_server_port_=%s!\n", http_server_port_.c_str());	
	}
};

Chttp_server_config http_server_config_;
bool http_server_is_run_ = false;
bool http_is_close_ = false;
std::thread tt_http_thread_;

//读取配置文件
static switch_status_t do_config(Chttp_server_config& http_server_config)
{
	std::string cf = "http_server.conf";
	switch_xml_t cfg, xml, param;
	switch_xml_t xml_profiles,xml_profile;

	if (!(xml = switch_xml_open_cfg(cf.c_str(), &cfg, NULL))) 
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "[do_config]Open of %s failed\n", cf.c_str());
		return SWITCH_STATUS_FALSE;
	}

    if ((xml_profiles = switch_xml_child(cfg, "profiles"))) 
	{
    	for (xml_profile = switch_xml_child(xml_profiles, "profile"); xml_profile;xml_profile = xml_profile->next) 
		{
            if (!(param = switch_xml_child(xml_profile, "param"))) 
			{
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_ERROR, "[do_config]No param, check the new config!\n");
                continue;
            }

			for (; param; param = param->next) 
			{
				char *var = (char *) switch_xml_attr_soft(param, "name");
				char *val = (char *) switch_xml_attr_soft(param, "value");

				if (!zstr(var) && !zstr(val)  ) 
				{
					if (!strcasecmp(var, "http_server")) 
					{
						http_server_config_.http_server_addr_ = val;
					} 
					else if (!strcasecmp(var, "http_port")) 
					{
						http_server_config_.http_server_port_ = val;
					} 								
				}			
			}						
		}
	}	

	switch_xml_free(xml);

	return SWITCH_STATUS_SUCCESS;
}

//启动http服务
int start_http_server()
{
	//去读配置文件
	switch_status_t do_config_return = do_config(http_server_config_);
	if(SWITCH_STATUS_SUCCESS != do_config_return)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[http_read_config]read_config fail.\n");
		return -1;
	}

	//启动一个线程
    tt_http_thread_ = std::thread([]()
    {
    	auto service = TcpService::Create();
    	service->startWorkerThread(1);

		auto httpEnterCallback = [](const HTTPParser& httpParser,
									const HttpSession::Ptr& session) 
		{
			//处理接收到的数据
			std::string http_recv_post_json = httpParser.getBody();

			switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[recv_post]http_recv_post_json=%s.\n", http_recv_post_json.c_str());

			//执行命令行
			std::string lua_return_json = "";
			auto run_ret_state = run_lua_file(http_recv_post_json, lua_return_json);

			HttpResponse response;
			if(false == run_ret_state)
			{
				response.setBody(std::string("{\"error\":\"run lua is error.\"}"));
			}
			else
			{
				response.setBody(lua_return_json);
			}

			if (httpParser.isKeepAlive())
			{
				response.addHeadValue("Content-Type", "application/json");
				response.addHeadValue("Connection", "Keep-Alive");
				session->send(response.getResult());
			}
			else
			{
				response.addHeadValue("Connection", "Close");
				session->send(response.getResult(), [session]() {
					session->postShutdown();
				});
			}
		};				

		wrapper::HttpListenerBuilder listenBuilder;
		listenBuilder
				.WithService(service)
				.AddSocketProcess([](TcpSocket& socket) {
					socket.setNodelay();
				})
				.WithMaxRecvBufferSize(1024)
				.WithAddr(false, http_server_config_.http_server_addr_.c_str(), atoi(http_server_config_.http_server_port_.c_str()))
				.WithReusePort()
				.WithEnterCallback([httpEnterCallback](const HttpSession::Ptr& httpSession, HttpSessionHandlers& handlers) {
					handlers.setHttpCallback(httpEnterCallback);
				})
				.asyncRun();

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[start]http_server=%s,port=%s is start.\n", 
			http_server_config_.http_server_addr_.c_str(),
			http_server_config_.http_server_port_.c_str());
		http_server_is_run_ = true;
		http_is_close_ = false;

		while (true)
		{
			std::this_thread::sleep_for(std::chrono::seconds(1));
			if (brynet::base::app_kbhit())
			{
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[stop]brynet::base::app_kbhit().\n"); 
				break;
			}

			if(true == http_is_close_)
			{
				//关闭http服务
				switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[stop]http_is_close_ is close.\n"); 
				break;
			}
		}

		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[stop]http_server=%s,port=%s is start.\n", 
			http_server_config_.http_server_addr_.c_str(),
			http_server_config_.http_server_port_.c_str());
		http_server_is_run_ = false;

    });	

	return 0;
}

//向队列里push消息
SWITCH_STANDARD_API(http_server_state) 
{
	if(true == http_server_is_run_)
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[http_server_state]http_server=%s,port=%s is start.\n", 
			http_server_config_.http_server_addr_.c_str(),
			http_server_config_.http_server_port_.c_str());
	}
	else
	{
		switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[http_server_state]http_server=%s,port=%s is stop.\n", 
			http_server_config_.http_server_addr_.c_str(),
			http_server_config_.http_server_port_.c_str());		
	}

	return SWITCH_STATUS_SUCCESS;
}

//向队列里push消息
SWITCH_STANDARD_API(http_server_reset) 
{
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[http_server_reset]http shoutdown begin!\n");
	if(true == http_server_is_run_)
	{
		http_is_close_ = true;
		tt_http_thread_.join();
	}
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[http_server_reset]http shoutdown end!\n");

	start_http_server();

	return SWITCH_STATUS_SUCCESS;
}


SWITCH_MODULE_LOAD_FUNCTION(mod_http_server_load)
{
	switch_api_interface_t* commands_api_interface;

	/* connect my internal structure to the blank pointer passed to me */
	*module_interface = switch_loadable_module_create_module_interface(pool, modname);

	SWITCH_ADD_API(commands_api_interface, "http_server_reset", "reset_http server", http_server_reset, "NULL");
	SWITCH_ADD_API(commands_api_interface, "http_server_state", "show http server state", http_server_state, "NULL");

  	/* 启动http服务 */
	start_http_server();

	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[mod_http_server_load]load http success!\n");

	/* indicate that the module should continue to be loaded */
	return SWITCH_STATUS_SUCCESS;
}

/* Called when the system shuts down */
SWITCH_MODULE_SHUTDOWN_FUNCTION(mod_http_server_shutdown)
{
	if(true == http_server_is_run_)
	{
		http_is_close_ = true;
		tt_http_thread_.join();
	}

	http_is_close_ = false;
	switch_log_printf(SWITCH_CHANNEL_LOG, SWITCH_LOG_NOTICE, "[mod_http_server_shutdown]unload http ok!\n");
	return SWITCH_STATUS_SUCCESS;
}

/*
  If it exists, this is called in it's own thread when the module-load completes
  If it returns anything but SWITCH_STATUS_TERM it will be called again automaticly
SWITCH_MODULE_RUNTIME_FUNCTION(mod_example_runtime);
{
	while(looping)
	{
		switch_yield(1000);
	}
	return SWITCH_STATUS_TERM;
}
*/
