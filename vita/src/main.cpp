// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonfin/app.hpp"
#include "moonfin/player.hpp"
#include "moonfin/runtime.hpp"
#include <clocale>
#include <cstdio>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/power.h>

// 256 MiB leaves space for GXM, decoder surfaces, and system services.
unsigned int _newlib_heap_size_user = 256 * 1024 * 1024;

int main(int, char **) {
  std::setlocale(LC_NUMERIC, "C");
  sceIoMkdir("ux0:data/moonfin", 0777);
  const std::string path = "ux0:data/moonfin/settings.json";
  moonfin::Settings config;
  std::string configError;
  try {
    config = moonfin::Settings::load(path);
  } catch (...) {
    configError = "Your settings file could not be read. It has been kept as "
                  "settings.json.bak.";
    sceIoRename(path.c_str(), (path + ".bak").c_str());
  }
  brls::Platform::APP_LOCALE_DEFAULT = brls::LOCALE_EN_US;
  brls::Application::setGlobalQuit(false);
  if (!brls::Application::init())
    return 1;
  brls::Application::getPlatform()->setThemeVariant(brls::ThemeVariant::DARK);
  brls::Application::createWindow("Moonfin Vita");
  if (auto *log = std::fopen("ux0:data/moonfin/moonfin.log", "w"))
    brls::Logger::setLogOutput(log);
  auto theme = brls::Theme::getDarkTheme();
  theme.addColor("brls/background", nvgRGB(9, 13, 24));
  theme.addColor("brls/highlight", nvgRGB(74, 173, 255));
  auto transport =
      std::make_shared<moonfin::CurlTransport>("app0:resources/cacert.pem");
  moonfin::Jobs network;
  moonfin::jobs = &network;
  std::unique_ptr<moonfin::Player> player;
  // All resources must be released before Borealis destroys the GXM context.
  auto exitSub = brls::Application::getExitEvent()->subscribe([&] {
    if (player)
      player->stop();
    moonfin::stopApp();
    player.reset();
    network.shutdown();
  });
  try {
    player = std::make_unique<moonfin::Player>();
    moonfin::runApp(config, path, transport, *player);
    if (!configError.empty())
      brls::Application::notify(configError);
    for (;;) {
      network.drain();
      moonfin::tickApp();
      player->pump();
      if (!brls::Application::mainLoop())
        break;
    }
  } catch (const std::exception &e) {
    moonfin::stopApp();
    player.reset();
    brls::Logger::error("Moonfin startup failed: {}", e.what());
    auto *d =
        new brls::Dialog(std::string("Moonfin could not start: ") + e.what());
    d->addButton("Close", [] { brls::Application::quit(); });
    d->open();
    while (brls::Application::mainLoop()) {
    }
  }
  brls::Application::getExitEvent()->unsubscribe(exitSub);
  moonfin::jobs = nullptr;
  sceKernelExitProcess(0);
  return 0;
}
