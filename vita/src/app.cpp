// SPDX-License-Identifier: GPL-3.0-or-later
#include "moonfin/app.hpp"
#include "moonfin/player.hpp"
#include "moonfin/runtime.hpp"
#include <algorithm>
#include <borealis.hpp>
#include <cmath>
#include <ctime>
#include <sstream>

namespace moonfin {
namespace {
using namespace brls;
struct App;
App *app = nullptr;
void accounts(bool reset = false);
void home();
void library(std::string title, Params query, int start = 0);
void detail(Json);
void settings();
void startPlayback(Json, PlaybackOptions, int64_t);
void liveTv();
void search();
void discover();

void message(const std::string &text) {
  auto *dialog = new Dialog(text);
  dialog->addButton("OK", [dialog] { dialog->close(); });
  dialog->open();
}
void choose(const std::string &title, const std::vector<std::string> &options,
            std::function<void(int)> cb, int selected = 0) {
  Application::pushActivity(new Activity(new Dropdown(
      title, options,
      [cb](int i) {
        if (i >= 0)
          cb(i);
      },
      selected)));
}
void textInput(const std::string &title, const std::string &initial,
               std::function<void(std::string)> cb, bool password = false) {
  auto *ime = Application::getImeManager();
  bool opened =
      password ? ime->openForPassword(std::move(cb), title, "", 128, initial)
               : ime->openForText(std::move(cb), title, "", 256, initial);
  if (!opened)
    message("The system keyboard could not open. Close any other system dialog "
            "and try again.");
}
Label *label(const std::string &text, float size = 23) {
  auto *l = new Label();
  l->setText(text);
  l->setFontSize(size);
  l->setMarginBottom(12);
  return l;
}
class MenuButton : public Button {
  std::shared_ptr<int> alive = std::make_shared<int>(0);

public:
  explicit MenuButton(std::function<void()> cb) {
    auto weak = std::weak_ptr<int>(alive);
    registerClickAction([cb = std::move(cb), weak](View *) {
      // Input dispatch still accesses this button and its action after return.
      // Run screen changes on the next drain, only if the button still exists.
      jobs->complete([cb, weak] {
        if (!weak.expired())
          cb();
      });
      return true;
    });
  }
  ~MenuButton() override { alive.reset(); }
};
Button *button(Box *parent, const std::string &text, std::function<void()> cb,
               float width = 0) {
  auto *b = new MenuButton(std::move(cb));
  b->setText(text);
  b->setHeight(54);
  if (width)
    b->setWidth(width);
  b->setMargins(0, 10, 10, 0);
  parent->addView(b);
  if (!Application::getCurrentFocus()) {
    const auto stack = Application::getActivitiesStack();
    if (!stack.empty() && b->getParentActivity() == stack.back())
      Application::giveFocus(b);
  }
  return b;
}
std::string clockText(double seconds) {
  int s = std::max(0, static_cast<int>(seconds));
  char out[32];
  if (s >= 3600)
    std::snprintf(out, sizeof(out), "%d:%02d:%02d", s / 3600, s / 60 % 60,
                  s % 60);
  else
    std::snprintf(out, sizeof(out), "%d:%02d", s / 60, s % 60);
  return out;
}
Json listOf(const Json &r) {
  if (r.is_array())
    return r;
  auto v = r.value("Items", Json::array());
  return v.is_array() ? v : Json::array();
}
std::string itemTitle(const Json &j) {
  auto name = stringValue(j, "Name", "Untitled");
  if (stringValue(j, "Type") == "Episode")
    return stringValue(j, "SeriesName") + " · S" +
           std::to_string(integerValue(j, "ParentIndexNumber")) + " E" +
           std::to_string(integerValue(j, "IndexNumber")) + " · " + name;
  return name;
}
class Page : public Activity {
public:
  std::shared_ptr<int> alive = std::make_shared<int>(0);
  Box *body = nullptr;
  AppletFrame *frame = nullptr;
  explicit Page(const std::string &title, bool root = false) {
    body = new Box(Axis::COLUMN);
    body->setPadding(20, 32, 32, 32);
    auto *scroll = new ScrollingFrame();
    scroll->setContentView(body);
    frame = new AppletFrame(scroll);
    frame->setTitle(title);
    frame->setBackgroundColor(nvgRGB(9, 13, 24));
    frame->registerAction("Back", BUTTON_B, [root](View *) {
      if (root) {
        auto *d = new Dialog("Close Moonfin Vita?");
        d->addButton("Cancel", [d] { d->close(); });
        d->addButton("Close", [d] { d->close([] { Application::quit(); }); });
        d->open();
      } else
        Application::popActivity();
      return true;
    });
  }
  View *createContentView() override { return frame; }
  ~Page() override { alive.reset(); }
  void load(std::function<Json()> work, std::function<void(const Json &)> done,
            bool clear = true) {
    if (clear) {
      Application::giveFocus(nullptr);
      body->clearViews();
      auto *loading = label("Loading…");
      loading->setFocusable(true);
      body->addView(loading);
      Application::giveFocus(loading);
    }
    auto weak = std::weak_ptr<int>(alive);
    bool queued = jobs->submit(
        [this, weak, work = std::move(work), done = std::move(done), clear] {
          if (weak.expired())
            return;
          try {
            auto r = work();
            jobs->complete([this, weak, r = std::move(r), done, clear] {
              if (weak.expired())
                return;
              if (clear) {
                Application::giveFocus(nullptr);
                body->clearViews();
              }
              try {
                done(r);
              } catch (const std::exception &e) {
                message(e.what());
                return;
              }
              if (clear && !weak.expired())
                Application::giveFocus(body);
            });
          } catch (const std::exception &e) {
            std::string error = e.what();
            jobs->complete([this, weak, error, work, done, clear] {
              if (weak.expired())
                return;
              if (clear) {
                Application::giveFocus(nullptr);
                body->clearViews();
                body->addView(label(error));
                button(body, "Retry", [this, work, done] { load(work, done); });
                Application::giveFocus(body);
              } else
                message(error);
            });
          }
        });
    if (!queued)
      message("The network queue is busy. Please try again.");
  }
};
struct App {
  Settings *config;
  std::string configPath;
  std::shared_ptr<Transport> http;
  std::shared_ptr<Client> client;
  Player *player;
  bool resumeAfterSuspend = false;
  std::shared_ptr<std::atomic<int>> focusChange =
      std::make_shared<std::atomic<int>>(-1);
  Event<bool>::Subscription focusSub;
  void save() {
    try {
      config->save(configPath);
    } catch (const std::exception &e) {
      message(e.what());
    }
  }
  PlaybackOptions options() const {
    PlaybackOptions o;
    o.bitrate = config->bitrate;
    o.height = config->height;
    o.forceTranscode = config->forceTranscode;
    return o;
  }
  void use(const Account &a) {
    client = std::make_shared<Client>(http, a, config->deviceId);
  }
};

class Card : public Box {
  std::shared_ptr<int> alive = std::make_shared<int>(0);
  std::shared_ptr<Client> client;
  Json item;
  Image *art;
  bool requested = false;

public:
  Card(std::shared_ptr<Client> c, Json data)
      : Box(Axis::COLUMN), client(std::move(c)), item(std::move(data)) {
    setWidth(202);
    setHeight(335);
    setMarginRight(18);
    setFocusable(true);
    art = new Image();
    art->setWidth(202);
    art->setHeight(272);
    art->setScalingType(ImageScalingType::FILL);
    art->setBackgroundColor(nvgRGB(24, 34, 52));
    addView(art);
    auto *l = label(itemTitle(item), 21);
    l->setHeight(57);
    l->setSingleLine(false);
    addView(l);
    registerClickAction([this](View *) {
      detail(item);
      return true;
    });
    addGestureRecognizer(new TapGestureRecognizer(this));
  }
  ~Card() override { alive.reset(); }
  void draw(NVGcontext *vg, float x, float y, float w, float h, Style style,
            FrameContext *ctx) override {
    if (!requested && y + h > 0 && y < 720 && x + w > 0 && x < 1280) {
      requested = true;
      auto weak = std::weak_ptr<int>(alive);
      auto c = client;
      auto it = item;
      if (!jobs->submit(
              [this, weak, c, it] {
                if (weak.expired())
                  return;
                try {
                  auto data = c->image(it, 160);
                  if (data.empty())
                    return;
                  jobs->complete([this, weak, data = std::move(data)] {
                    if (!weak.expired())
                      art->setImageFromMem(
                          reinterpret_cast<const unsigned char *>(data.data()),
                          data.size());
                  });
                } catch (...) {
                }
              },
              false))
        requested = false;
    }
    Box::draw(vg, x, y, w, h, style, ctx);
  }
};
void cards(Page *p, const Json &items) {
  if (items.empty()) {
    p->body->addView(label("No items found."));
    return;
  }
  Box *row = nullptr;
  int index = 0;
  auto client = app->client;
  for (auto &j : items) {
    if (index++ % 5 == 0) {
      row = new Box(Axis::ROW);
      row->setMarginBottom(18);
      p->body->addView(row);
    }
    row->addView(new Card(client, j));
  }
}
void shelf(Page *p, const std::string &title, const Json &items) {
  if (items.empty())
    return;
  p->body->addView(label(title, 28));
  auto *scroll = new HScrollingFrame();
  scroll->setHeight(350);
  auto *row = new Box(Axis::ROW);
  for (const auto &j : items)
    row->addView(new Card(app->client, j));
  scroll->setContentView(row);
  p->body->addView(scroll);
}
void afterLogin(Account account) {
  auto &s = *app->config;
  auto it =
      std::find_if(s.accounts.begin(), s.accounts.end(), [&](const Account &a) {
        return a.url == account.url && a.userId == account.userId;
      });
  if (it == s.accounts.end()) {
    s.accounts.push_back(account);
    s.active = s.accounts.size() - 1;
  } else {
    *it = account;
    s.active = static_cast<int>(it - s.accounts.begin());
  }
  app->save();
  app->use(account);
  home();
}
void quickConnect(Page *loginPage, std::shared_ptr<Client> client) {
  auto *p = new Page("Quick Connect");
  Application::pushActivity(p);
  p->load(
      [client] { return client->request("POST", "/QuickConnect/Initiate"); },
      [p, client](const Json &r) {
        const auto code = stringValue(r, "Code"),
                   secret = stringValue(r, "Secret");
        if (code.empty() || secret.empty())
          throw std::runtime_error(
              "Quick Connect is unavailable on this server.");
        p->body->addView(label(
            "Enter this code in an already signed-in Jellyfin client:", 27));
        p->body->addView(label(code, 58));
        p->body->addView(label("Open Settings → Quick Connect on the other "
                               "client, approve this code, then check below."));
        button(p->body, "Check approval", [p, client, secret] {
          p->load(
              [client, secret] {
                auto status = client->request("GET", "/QuickConnect/Connect",
                                              {{"Secret", secret}});
                if (!status.value("Authenticated", false))
                  throw std::runtime_error(
                      "This code has not been approved yet.");
                return Json(client->authenticateQuickConnect(secret));
              },
              [](const Json &r) { afterLogin(r.get<Account>()); }, false);
        });
      });
}
void login(const std::string &url) {
  auto *p = new Page("Connect to your server");
  Application::pushActivity(p);
  auto c = std::make_shared<Client>(app->http, Account{url, "", "", "", ""},
                                    app->config->deviceId);
  p->load(
      [c] { return c->serverInfo(); },
      [p, c, url](const Json &info) {
        auto account = c->account();
        account.serverName = stringValue(info, "ServerName", "Media server");
        auto client =
            std::make_shared<Client>(app->http, account, app->config->deviceId);
        p->frame->setTitle("Moonfin · " + account.serverName);
        p->body->addView(label(url));
        auto username = std::make_shared<std::string>();
        auto password = std::make_shared<std::string>();
        auto *userButton = button(p->body, "Username: select to enter", [] {});
        auto weak = std::weak_ptr<int>(p->alive);
        userButton->registerClickAction([weak, username, userButton](View *) {
          textInput("Username", *username,
                    [weak, username, userButton](std::string s) {
                      if (weak.expired())
                        return;
                      *username = s;
                      userButton->setText("Username: " + s);
                    });
          return true;
        });
        auto *passButton = button(p->body, "Password: select to enter", [] {});
        passButton->registerClickAction([weak, password, passButton](View *) {
          textInput(
              "Password", "",
              [weak, password, passButton](std::string s) {
                if (weak.expired())
                  return;
                *password = s;
                passButton->setText(s.empty() ? "Password: empty"
                                              : "Password: entered");
              },
              true);
          return true;
        });
        button(p->body, "Sign in", [p, client, username, password] {
          if (username->empty()) {
            message("Enter your username first.");
            return;
          }
          auto secret = *password;
          password->clear();
          p->load([client, user = *username,
                   secret] { return Json(client->login(user, secret)); },
                  [](const Json &r) { afterLogin(r.get<Account>()); }, false);
        });
        button(p->body, "Quick Connect (Jellyfin)",
               [p, client] { quickConnect(p, client); });
        p->body->addView(label("Jellyfin and Emby use the same server sign-in "
                               "form. HTTP is supported for local servers; "
                               "HTTPS verifies the server certificate."));
      });
}
void accounts(bool reset) {
  if (reset) {
    Application::giveFocus(nullptr);
    Application::clear();
  }
  auto *p = new Page("Moonfin Vita · Servers", reset);
  Application::pushActivity(p, TransitionAnimation::NONE);
  p->body->addView(label("Your media, on Vita", 36));
  for (size_t i = 0; i < app->config->accounts.size(); ++i) {
    auto a = app->config->accounts[i];
    button(p->body, a.serverName + " · " + a.userName, [p, a, i] {
      auto c = std::make_shared<Client>(app->http, a, app->config->deviceId);
      p->load(
          [c, a] {
            c->request("GET", "/Users/" + encode(a.userId));
            return Json(a);
          },
          [i, a](const Json &) {
            app->config->active = i;
            app->save();
            app->use(a);
            home();
          },
          false);
    });
  }
  button(p->body, "Add Jellyfin or Emby server", [] {
    textInput("Server URL, including http:// or https://", "http://",
              [](std::string s) {
                try {
                  login(normalizeServerUrl(s));
                } catch (const std::exception &e) {
                  message(e.what());
                }
              });
  });
  p->body->addView(
      label("Moonfin Vita " + std::string(version) +
            " · Native preview\nUse your existing Jellyfin or Emby server. No "
            "additional streaming proxy is required."));
  Application::giveFocus(p->body);
}
void libraries() {
  auto *p = new Page("Libraries");
  Application::pushActivity(p);
  auto c = app->client;
  p->load([c] { return c->views(); },
          [p](const Json &r) {
            for (auto &j : listOf(r))
              button(p->body, stringValue(j, "Name"), [j] {
                library(stringValue(j, "Name"),
                        {{"ParentId", stringValue(j, "Id")},
                         {"SortBy", "SortName"},
                         {"SortOrder", "Ascending"}});
              });
          });
}
void home() {
  Application::giveFocus(nullptr);
  Application::clear();
  auto *p = new Page("Moonfin · " + app->client->account().userName, true);
  Application::pushActivity(p, TransitionAnimation::NONE);
  auto *nav = new Box(Axis::ROW);
  p->body->addView(nav);
  button(nav, "Libraries", libraries, 165);
  button(nav, "Search", search, 150);
  button(
      nav, "Favorites",
      [] {
        library("Favorites", {{"Recursive", "true"},
                              {"Filters", "IsFavorite"},
                              {"SortBy", "SortName"}});
      },
      170);
  button(nav, "Live TV", liveTv, 150);
  button(nav, "Settings", settings, 165);
  // The page was pushed before its buttons existed. Establish live focus now,
  // without waiting for the asynchronous home/artwork requests to finish.
  Application::giveFocus(nav);
  auto c = app->client;
  p->load(
      [c] {
        Json r;
        r["resume"] = c->resume();
        r["next"] = c->nextUp();
        r["views"] = c->views();
        r["latest"] = Json::array();
        int count = 0;
        for (const auto &v : listOf(r["views"])) {
          if (count++ == 3)
            break;
          try {
            r["latest"].push_back({{"name", stringValue(v, "Name")},
                                   {"items", c->latest(stringValue(v, "Id"))}});
          } catch (...) {
          }
        }
        return r;
      },
      [p](const Json &r) {
        shelf(p, "Continue watching", listOf(r["resume"]));
        shelf(p, "Next up", listOf(r["next"]));
        for (const auto &row : r["latest"])
          shelf(p, "Latest in " + stringValue(row, "name"),
                listOf(row["items"]));
        button(p->body, "Discover and request with Seerr", discover);
        button(p->body, "Refresh home", home);
      },
      false);
}
void library(std::string title, Params query, int start) {
  auto *p = new Page(title);
  Application::pushActivity(p);
  auto c = app->client;
  query["StartIndex"] = std::to_string(start);
  query["Limit"] = "30";
  p->load([c, query] { return c->items(query); },
          [p, title, query, start](const Json &r) {
            cards(p, listOf(r));
            const auto total = integerValue(r, "TotalRecordCount");
            p->body->addView(label("Items " + std::to_string(start + 1) + "–" +
                                   std::to_string(start + listOf(r).size()) +
                                   " of " + std::to_string(total)));
            if (start + 30 < total)
              button(p->body, "Next page", [title, query, start] {
                Application::popActivity(TransitionAnimation::NONE,
                                         [title, query, start] {
                                           library(title, query, start + 30);
                                         });
              });
            if (start > 0)
              button(p->body, "Previous page", [title, query, start] {
                Application::popActivity(TransitionAnimation::NONE,
                                         [title, query, start] {
                                           library(title, query, start - 30);
                                         });
              });
          });
}
void search() {
  textInput("Search your library", "", [](std::string s) {
    if (!s.empty())
      library("Search: " + s,
              {{"SearchTerm", s},
               {"Recursive", "true"},
               {"IncludeItemTypes",
                "Movie,Series,Episode,MusicAlbum,MusicArtist,Audio"}});
  });
}
void children(const Json &item) {
  const auto type = stringValue(item, "Type"), id = stringValue(item, "Id");
  if (type == "Series") {
    auto *p = new Page(itemTitle(item));
    Application::pushActivity(p);
    auto c = app->client;
    p->load([c, id] { return c->seasons(id); },
            [p](const Json &r) { cards(p, listOf(r)); });
  } else
    library(itemTitle(item),
            {{"ParentId", id},
             {"SortBy", "ParentIndexNumber,IndexNumber,SortName"},
             {"SortOrder", "Ascending"}});
}
void detail(Json shallow) {
  auto *p = new Page(itemTitle(shallow));
  Application::pushActivity(p);
  auto c = app->client;
  p->load(
      [c, id = stringValue(shallow, "Id")] { return c->item(id); },
      [p, c](const Json &item) {
        const auto type = stringValue(item, "Type");
        const bool folder = item.value("IsFolder", false) || type == "Series" ||
                            type == "Season" || type == "MusicAlbum" ||
                            type == "MusicArtist";
        p->body->addView(label(itemTitle(item), 34));
        std::string meta = type;
        auto year = integerValue(item, "ProductionYear");
        if (year)
          meta += " · " + std::to_string(year);
        auto runtime = integerValue(item, "RunTimeTicks");
        if (runtime)
          meta +=
              " · " + clockText(runtime / static_cast<double>(ticksPerSecond));
        auto rating = stringValue(item, "OfficialRating");
        if (!rating.empty())
          meta += " · " + rating;
        p->body->addView(label(meta));
        auto user = item.value("UserData", Json::object());
        auto resume = integerValue(user, "PlaybackPositionTicks");
        if (folder)
          button(p->body, "Browse " + type, [item] { children(item); });
        else {
          const auto media = stringValue(item, "MediaType");
          if (media == "Video" || media == "Audio" || type == "TvChannel" ||
              type == "Movie" || type == "Episode" || type == "Audio") {
            if (resume > 0)
              button(p->body,
                     "Resume at " + clockText(resume / static_cast<double>(
                                                           ticksPerSecond)),
                     [item, resume] {
                       startPlayback(item, app->options(), resume);
                     });
            button(p->body, resume > 0 ? "Play from beginning" : "Play",
                   [item] { startPlayback(item, app->options(), 0); });
            auto sources = item.value("MediaSources", Json::array());
            if (sources.size() > 1)
              button(p->body, "Choose version", [item, sources] {
                std::vector<std::string> names;
                for (const auto &s : sources)
                  names.push_back(stringValue(s, "Name", stringValue(s, "Id")));
                choose("Version", names, [item, sources](int i) {
                  auto o = app->options();
                  o.sourceId = stringValue(sources[i], "Id");
                  startPlayback(item, o, 0);
                });
              });
          } else
            p->body->addView(label("This media type is not implemented in the "
                                   "native Vita preview yet."));
        }
        button(p->body,
               user.value("IsFavorite", false) ? "Remove favorite"
                                               : "Add favorite",
               [p, c, item, user] {
                 p->load(
                     [c, item, user] {
                       c->favorite(stringValue(item, "Id"),
                                   !user.value("IsFavorite", false));
                       return Json::object();
                     },
                     [](const Json &) { message("Favorite updated."); }, false);
               });
        if (type != "MusicArtist")
          button(p->body,
                 user.value("Played", false) ? "Mark unwatched"
                                             : "Mark watched",
                 [p, c, item, user] {
                   p->load(
                       [c, item, user] {
                         c->watched(stringValue(item, "Id"),
                                    !user.value("Played", false));
                         return Json::object();
                       },
                       [](const Json &) { message("Watch status updated."); },
                       false);
                 });
        auto overview = stringValue(item, "Overview");
        if (!overview.empty())
          p->body->addView(label(overview, 23));
        if (item.contains("People")) {
          std::string cast;
          int count = 0;
          for (const auto &person : item["People"]) {
            if (count++ == 10)
              break;
            if (!cast.empty())
              cast += " · ";
            cast += stringValue(person, "Name");
          }
          if (!cast.empty())
            p->body->addView(label("Cast & crew: " + cast, 21));
        }
      });
}

class PlayerView : public View {
  Json item;
  PlaybackOptions options;
  Playback playback;
  std::shared_ptr<int> alive = std::make_shared<int>(0);
  std::chrono::steady_clock::time_point visibleUntil;
  void reveal() {
    visibleUntil = std::chrono::steady_clock::now() + std::chrono::seconds(5);
  }
  void tracks() {
    std::vector<std::string> names = {"Audio track", "Subtitles", "Quality"};
    auto weak = std::weak_ptr<int>(alive);
    choose("Playback", names, [this, weak](int i) {
      if (weak.expired())
        return;
      if (i == 2) {
        choose("Quality",
               {"960×544 · 4 Mbps", "720p · 6 Mbps", "360p · 1 Mbps",
                "Force transcode · 544p"},
               [this, weak](int n) {
                 if (weak.expired())
                   return;
                 auto o = options;
                 o.height = n == 1 ? 720 : (n == 2 ? 360 : 544);
                 o.bitrate = n == 1 ? 6000000 : (n == 2 ? 1000000 : 4000000);
                 o.forceTranscode = n == 3;
                 restart(o);
               });
        return;
      }
      std::vector<std::string> labels;
      std::vector<int> indices;
      if (i == 1) {
        labels.push_back("Off");
        indices.push_back(-1);
      }
      for (const auto &s : playback.streams)
        if (stringValue(s, "Type") == (i == 0 ? "Audio" : "Subtitle")) {
          labels.push_back(stringValue(s, "DisplayTitle",
                                       stringValue(s, "Language", "Unknown")) +
                           " · " + stringValue(s, "Codec"));
          indices.push_back(integerValue(s, "Index", -1));
        }
      if (labels.empty()) {
        message("No tracks were reported for this source.");
        return;
      }
      choose(i == 0 ? "Audio track" : "Subtitles (rendered by server)", labels,
             [this, weak, indices, i](int n) {
               if (weak.expired())
                 return;
               auto o = options;
               if (i == 0)
                 o.audioIndex = indices[n];
               else
                 o.subtitleIndex = indices[n];
               restart(o);
             });
    });
  }
  void restart(PlaybackOptions o) {
    const int64_t ticks = app->player->position() * ticksPerSecond;
    auto data = item;
    // Closing the old player first reports its final position and releases its
    // transcode.
    app->player->stop();
    Application::popActivity(TransitionAnimation::NONE, [data, o, ticks] {
      startPlayback(data, o, ticks);
    });
  }

public:
  PlayerView(Json j, PlaybackOptions o, Playback p)
      : item(std::move(j)), options(std::move(o)), playback(std::move(p)) {
    setFocusable(true);
    setHideHighlight(true);
    setGrow(1);
    reveal();
    registerAction("Play / pause", BUTTON_A, [this](View *) {
      app->player->togglePause();
      reveal();
      return true;
    });
    registerAction("Back", BUTTON_B, [](View *) {
      app->player->stop();
      Application::popActivity();
      return true;
    });
    registerAction("Tracks & quality", BUTTON_X, [this](View *) {
      tracks();
      reveal();
      return true;
    });
    registerAction("Skip intro / credits", BUTTON_Y, [this](View *) {
      app->player->skipSegment();
      reveal();
      return true;
    });
    registerAction("Controls", BUTTON_START, [this](View *) {
      reveal();
      return true;
    });
    registerAction("Seek -10s", BUTTON_NAV_LEFT, [this](View *) {
      app->player->seek(-10);
      reveal();
      return true;
    });
    registerAction("Seek +10s", BUTTON_NAV_RIGHT, [this](View *) {
      app->player->seek(10);
      reveal();
      return true;
    });
    registerAction("Seek -60s", BUTTON_LB, [this](View *) {
      app->player->seek(-60);
      reveal();
      return true;
    });
    registerAction("Seek +60s", BUTTON_RB, [this](View *) {
      app->player->seek(60);
      reveal();
      return true;
    });
    registerAction("Volume up", BUTTON_NAV_UP, [this](View *) {
      app->player->volume(5);
      reveal();
      return true;
    });
    registerAction("Volume down", BUTTON_NAV_DOWN, [this](View *) {
      app->player->volume(-5);
      reveal();
      return true;
    });
    addGestureRecognizer(new TapGestureRecognizer(this));
    registerClickAction([this](View *) {
      app->player->togglePause();
      reveal();
      return true;
    });
    auto weak = std::weak_ptr<int>(alive);
    app->player->onEnd = [this, weak](bool eof) {
      if (weak.expired())
        return;
      const auto error = app->player->error();
      auto data = item;
      Application::popActivity(TransitionAnimation::NONE, [data, error, eof] {
        if (!eof) {
          message(error);
          return;
        }
        if (stringValue(data, "Type") != "Episode")
          return;
        auto c = app->client;
        auto *p = new Page("Next episode");
        Application::pushActivity(p);
        p->load(
            [c, data] { return c->episodes(stringValue(data, "SeriesId")); },
            [p, data](const Json &r) {
              auto list = listOf(r);
              for (size_t i = 0; i + 1 < list.size(); ++i)
                if (stringValue(list[i], "Id") == stringValue(data, "Id")) {
                  auto next = list[i + 1];
                  button(p->body, "Play next: " + itemTitle(next),
                         [next] { startPlayback(next, app->options(), 0); });
                  return;
                }
              p->body->addView(
                  label("You have reached the end of this series."));
            });
      });
    };
    app->player->onState = [this, weak] {
      if (!weak.expired())
        reveal();
    };
  }
  ~PlayerView() override {
    alive.reset();
    if (app && app->player) {
      app->player->onEnd = nullptr;
      app->player->onState = nullptr;
      app->player->stop();
    }
  }
  void draw(NVGcontext *vg, float x, float y, float w, float h, Style,
            FrameContext *) override {
    auto &player = *app->player;
    player.draw(vg, x, y, w, h);
    const auto segment = player.activeSegment();
    if (std::chrono::steady_clock::now() > visibleUntil && !player.paused() &&
        segment.empty())
      return;
    nvgBeginPath(vg);
    nvgRect(vg, x, y + h - 160, w, 160);
    nvgFillColor(vg, nvgRGBA(6, 10, 20, 225));
    nvgFill(vg);
    nvgFontFaceId(vg, Application::getFont(FONT_REGULAR));
    nvgTextAlign(vg, NVG_ALIGN_LEFT | NVG_ALIGN_TOP);
    nvgFontSize(vg, 27);
    nvgFillColor(vg, nvgRGB(245, 246, 250));
    nvgText(vg, x + 28, y + h - 145, player.title.c_str(), nullptr);
    const auto status = (player.paused() ? "Paused · " : "") +
                        clockText(player.position()) + " / " +
                        clockText(player.duration()) + " · " + player.method;
    nvgFontSize(vg, 22);
    nvgText(vg, x + 28, y + h - 108, status.c_str(), nullptr);
    nvgBeginPath(vg);
    nvgRect(vg, x + 28, y + h - 70, w - 56, 5);
    nvgFillColor(vg, nvgRGB(60, 66, 78));
    nvgFill(vg);
    if (player.duration() > 0) {
      nvgBeginPath(vg);
      nvgRect(vg, x + 28, y + h - 70,
              (w - 56) *
                  std::clamp(player.position() / player.duration(), 0.0, 1.0),
              5);
      nvgFillColor(vg, nvgRGB(73, 173, 255));
      nvgFill(vg);
    }
    nvgFontSize(vg, 19);
    const std::string help = "Cross: pause  ·  D-pad: seek/volume  ·  Square: "
                             "tracks  ·  Circle: back";
    nvgText(vg, x + 28, y + h - 46, help.c_str(), nullptr);
    if (!segment.empty()) {
      nvgFontSize(vg, 24);
      nvgText(vg, x + 28, y + 32, ("Triangle: Skip " + segment).c_str(),
              nullptr);
    }
  }
};
void startPlayback(Json item, PlaybackOptions options, int64_t resume) {
  auto *p = new Page("Preparing playback");
  Application::pushActivity(p);
  auto c = app->client;
  p->load(
      [c, item, options, resume] {
        auto r = c->resolve(item, options, resume);
        return Json{{"itemId", r.itemId},
                    {"sourceId", r.sourceId},
                    {"playSessionId", r.playSessionId},
                    {"liveStreamId", r.liveStreamId},
                    {"url", r.url},
                    {"method", r.method},
                    {"streams", r.streams},
                    {"startTicks", r.startTicks},
                    {"audioIndex", r.audioIndex},
                    {"subtitleIndex", r.subtitleIndex}};
      },
      [c, item, options](const Json &r) {
        Playback play;
        play.itemId = r["itemId"];
        play.sourceId = r["sourceId"];
        play.playSessionId = r["playSessionId"];
        play.liveStreamId = r["liveStreamId"];
        play.url = r["url"];
        play.method = r["method"];
        play.streams = r["streams"];
        play.startTicks = r["startTicks"];
        play.audioIndex = r["audioIndex"];
        play.subtitleIndex = r["subtitleIndex"];
        Application::popActivity(
            TransitionAnimation::NONE, [c, item, options, play] {
              try {
                app->player->open(c, item, play);
                Application::pushActivity(
                    new Activity(new PlayerView(item, options, play)),
                    TransitionAnimation::NONE);
              } catch (const std::exception &e) {
                app->player->stop();
                message(e.what());
              }
            });
      });
}
void liveTv() {
  auto *p = new Page("Live TV");
  Application::pushActivity(p);
  auto c = app->client;
  button(p->body, "Channels", [c] {
    auto *page = new Page("Live channels");
    Application::pushActivity(page);
    page->load(
        [c] {
          return c->request("GET", "/LiveTv/Channels",
                            {{"UserId", c->account().userId},
                             {"Limit", "60"},
                             {"AddCurrentProgram", "true"}});
        },
        [page](const Json &r) {
          for (auto j : listOf(r)) {
            const auto program = j.value("CurrentProgram", Json::object());
            button(page->body,
                   stringValue(j, "Number") + " · " + stringValue(j, "Name") +
                       " — " + stringValue(program, "Name"),
                   [j] { detail(j); });
          }
        });
  });
  button(p->body, "Recordings", [c] {
    auto *page = new Page("Recordings");
    Application::pushActivity(page);
    page->load(
        [c] {
          return c->request("GET", "/LiveTv/Recordings",
                            {{"UserId", c->account().userId}, {"Limit", "30"}});
        },
        [page](const Json &r) { cards(page, listOf(r)); });
  });
  p->body->addView(
      label("Live TV requires a tuner configured on your server. Channel "
            "playback uses the server's Vita-compatible stream."));
}
void settings() {
  auto *p = new Page("Moonfin Vita settings");
  Application::pushActivity(p);
  button(p->body, "Streaming quality", [] {
    choose("Streaming quality",
           {"544p · 4 Mbps (default)", "720p · 6 Mbps", "360p · 1 Mbps"},
           [](int i) {
             app->config->height = i == 1 ? 720 : (i == 2 ? 360 : 544);
             app->config->bitrate =
                 i == 1 ? 6000000 : (i == 2 ? 1000000 : 4000000);
             app->save();
           });
  });
  button(p->body, "Transcoding mode", [] {
    choose(
        "Transcoding",
        {"Automatic: direct play compatible files", "Always transcode"},
        [](int i) {
          app->config->forceTranscode = i == 1;
          app->save();
        },
        app->config->forceTranscode ? 1 : 0);
  });
  button(p->body, "Switch or add server", [] { accounts(true); });
  button(p->body, "Remove this saved account", [] {
    auto *d = new Dialog(
        "Remove this account and its saved access token from this Vita?");
    d->addButton("Cancel", [d] { d->close(); });
    d->addButton("Remove", [d] {
      d->close([] {
        auto &s = *app->config;
        if (s.active >= 0)
          s.accounts.erase(s.accounts.begin() + s.active);
        s.active = -1;
        app->save();
        app->client.reset();
        accounts(true);
      });
    });
    d->open();
  });
  button(p->body, "About", [] {
    message("Moonfin Vita " + std::string(version) +
            "\nNative frontend for Moonfin Core, Jellyfin and "
            "Emby.\nInterface: Borealis · Playback: Vita mpv/GXM\nSource: "
            "github.com/TheEelYT/Moonfin-Core-Vita\nSee vita/PORT_STATUS.md "
            "for implemented features and pending work.");
  });
  p->body->addView(label(
      "Subtitles are rendered by the server to preserve ASS styling and image "
      "subtitles. Select them with Square during playback. Selecting an audio "
      "track or subtitles renegotiates the stream at your current position."));
}
void discover() {
  auto *p = new Page("Seerr discovery");
  Application::pushActivity(p);
  auto c = app->client;
  button(p->body, "Search Seerr", [p, c] {
    textInput(
        "Search Seerr", "",
        [p, c, weak = std::weak_ptr<int>(p->alive)](std::string query) {
          if (weak.expired() || query.empty())
            return;
          p->load(
              [c, query] {
                return c->request("GET", "/Moonfin/Seerr/Api/search",
                                  {{"query", query}, {"page", "1"}});
              },
              [p, c](const Json &raw) {
                auto r = raw;
                if (r.contains("data"))
                  r = r["data"];
                for (const auto &result : r.value("results", Json::array())) {
                  const auto type = stringValue(result, "mediaType");
                  if (type != "movie" && type != "tv")
                    continue;
                  const auto name =
                      stringValue(result, "title", stringValue(result, "name"));
                  button(p->body, name + " · " + type, [c, result, name, type] {
                    auto *d = new Dialog("Request " + name + " through Seerr?");
                    d->addButton("Cancel", [d] { d->close(); });
                    d->addButton("Request", [d, c, result, type] {
                      d->close([c, result, type] {
                        auto *req = new Page("Requesting media");
                        Application::pushActivity(req);
                        req->load(
                            [c, result, type] {
                              Json body = {
                                  {"mediaType", type},
                                  {"mediaId", integerValue(result, "id")}};
                              if (type == "tv")
                                body["seasons"] = "all";
                              return c->request("POST",
                                                "/Moonfin/Seerr/Api/request",
                                                {}, body);
                            },
                            [req](const Json &) {
                              req->body->addView(
                                  label("Request submitted. Your server's "
                                        "Seerr rules control approval."));
                            });
                      });
                    });
                    d->open();
                  });
                }
              },
              false);
        });
  });
  p->body->addView(
      label("Requires the Moonbase server plugin with Seerr configured. "
            "Requests use your server identity and Seerr permissions."));
}
} // namespace

void runApp(Settings &settings, const std::string &path,
            std::shared_ptr<Transport> http, Player &player) {
  app = new App();
  app->config = &settings;
  app->configPath = path;
  app->http = std::move(http);
  app->player = &player;
  // Power events originate on the Vita callback thread. Only set an atomic
  // here.
  app->focusSub = Application::getWindowFocusChangedEvent()->subscribe(
      [state = app->focusChange](bool focus) { *state = focus ? 1 : 0; });
  accounts(true);
}
void tickApp() {
  if (!app)
    return;
  auto focus = app->focusChange->exchange(-1);
  if (focus == 0 && app->player->active()) {
    app->resumeAfterSuspend = !app->player->paused();
    if (app->resumeAfterSuspend)
      app->player->togglePause();
  } else if (focus == 1 && app->resumeAfterSuspend && app->player->active()) {
    app->resumeAfterSuspend = false;
    if (app->player->paused())
      app->player->togglePause();
  }
}
void stopApp() {
  if (!app)
    return;
  Application::getWindowFocusChangedEvent()->unsubscribe(app->focusSub);
  Application::giveFocus(nullptr);
  Application::clear();
  delete app;
  app = nullptr;
}
} // namespace moonfin
