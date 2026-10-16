#include <spdlog/sinks/basic_file_sink.h>
#include "hooks.h"

namespace {
	void InitLogging(std::string_view name) {
		auto path = *SKSE::log::log_directory() / std::format("{}.log", name);
		auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(path.string(), true);
		spdlog::set_default_logger(std::make_shared<spdlog::logger>("log", std::move(sink)));
		spdlog::set_level(spdlog::level::info);
		spdlog::flush_on(spdlog::level::info);
	}

	void OnSKSEMessage(SKSE::MessagingInterface::Message *message) {
		switch (message->type) {
			case SKSE::MessagingInterface::kInputLoaded: {
				auto success = InstallHooks();
				if (success) logger::info("Initialization complete.");
				else logger::info("Initialization aborted.");
				break;
			}
		}
	}
}

SKSEPluginLoad(const SKSE::LoadInterface* skse) {
	SKSE::Init(skse);
	auto plugin = SKSE::PluginDeclaration::GetSingleton();
	InitLogging(plugin->GetName());
	logger::info("Initializing version: {}.", plugin->GetVersion());
	SKSE::GetMessagingInterface()->RegisterListener(OnSKSEMessage);
	return true;
}
