/**
 * @file main.cpp
 * @brief assists with testing MEGA CloudRAID
 *
 * (c) 2013-2019 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#include <asio.hpp>
#include <fstream>
#include <thread>
#include <chrono>
#include <deque>
#include <vector>
#include <mutex>
#include "TcpRelay.h"
#include <windns.h>
#include <codecvt>
#include <mega.h>
#include <mega/logging.h>
#include <windns.h>
#include <iomanip>
#include <regex>

using namespace std;
using namespace ::mega;
namespace ac = ::mega::autocomplete;

void AsioThreadRunFunction(asio::io_service& asio_service, const std::string& name)
{
    try
    {
        asio_service.run();
    }
    catch (std::exception& e)
    {
        cout << "Asio service '" << name << "' exception: " << e.what() << endl;
    }
    catch (...)
    {
        cout << "Asio service '" << name << "' unknown exception." << endl;
    }
    cout << "Asio service '" << name << "' finished." << endl;
}

struct RelayRunner
{
    asio::io_service asio_service;											// a single service object so all socket callbacks are serviced by just one thread
    std::unique_ptr<asio::io_service::work> asio_dont_exit;
    asio::steady_timer logTimer;

    mutex relaycollectionmutex;
    vector<unique_ptr<TcpRelayAcceptor>> relayacceptors;
    vector<unique_ptr<TcpRelay>> acceptedrelays;

    RelayRunner()
        : asio_dont_exit(std::make_unique<asio::io_service::work>(asio_service))
        , logTimer(asio_service)
    {
        StartLogTimer();
    }

    void AddAcceptor(const string& name, uint16_t port, asio::ip::address_v6 targetAddress, bool start)
    {
        lock_guard g(relaycollectionmutex);
        relayacceptors.emplace_back(new TcpRelayAcceptor(asio_service, name, port, asio::ip::tcp::endpoint(targetAddress, 80),
            [this](unique_ptr<TcpRelay> p)
        {
            lock_guard g(relaycollectionmutex);
            acceptedrelays.emplace_back(move(p));
            cout << acceptedrelays.back()->reporting_name << " acceptor is #" << (acceptedrelays.size() - 1) << endl;
        }));

        cout << "Acceptor active on " << port << ", relaying to " << name << endl;

        if (start)
        {
            relayacceptors.back()->Start();
        }
    }

    void RunRelays()
    {
        // keep these on their own thread & io_service so app glitches don't slow them down
        AsioThreadRunFunction(asio_service, "Relays");
    }

    void Stop()
    {
        asio_dont_exit.reset();  // remove the last work item so the service's run() will return
        asio_service.stop();  // interrupt asio's current wait for something to do
    }

    void StartLogTimer()
    {
        logTimer.expires_from_now(std::chrono::seconds(5));
        logTimer.async_wait([this](const asio::error_code&) { Log(); });
    }

    void Log()
    {
        lock_guard g(relaycollectionmutex);
        size_t eversent = 0, everreceived = 0;
        size_t sendRate = 0, receiveRate = 0;
        int senders = 0, receivers = 0, active = 0;
        for (size_t i = acceptedrelays.size(); i--; )
        {
            if (!acceptedrelays[i]->stopped)
            {
                size_t s = acceptedrelays[i]->connect_side.send_rate_buckets.CalculatRate();
                size_t r = acceptedrelays[i]->acceptor_side.send_rate_buckets.CalculatRate();
                if (s) senders += 1;
                if (r) receivers += 1;
                sendRate += s;
                receiveRate += r;
                active += 1;
            }
            eversent += acceptedrelays[i]->acceptor_side.totalbytes;
            everreceived += acceptedrelays[i]->connect_side.totalbytes;
        }
        cout << "active: " << active << " senders: " << senders << " rate " << sendRate << " receivers: " << receivers << " rate " << receiveRate << " totals: " << eversent << " " << everreceived << endl;
        StartLogTimer();
    }

    void report()
    {
        lock_guard g(relaycollectionmutex);
        for (auto& r : acceptedrelays)
        {
            cout << " " << r->reporting_name << ": " << r->acceptor_side.totalbytes << " " << r->connect_side.totalbytes << " " << (r->stopped ? "stopped" : "active") << (r->paused ? " (paused)" : "") << endl;
        }
    }
};

RelayRunner g_relays;


void addRelay(const string& server, uint16_t port)
{
    DNS_RECORD* pDnsrec;
    DNS_STATUS dnss = DnsQuery_A(server.c_str(), DNS_TYPE_A, DNS_QUERY_STANDARD, NULL,  &pDnsrec, NULL);
    
    if (dnss || !pDnsrec)
    {
        cout << "dns error" << endl;
        return;
    }
    
    asio::ip::address_v6::bytes_type bytes;
    for (auto& b : bytes) b = 0;
    bytes[10] = 0xff;
    bytes[11] = 0xff;
    bytes[12] = *(reinterpret_cast<unsigned char*>(&pDnsrec->Data.A.IpAddress) + 0);
    bytes[13] = *(reinterpret_cast<unsigned char*>(&pDnsrec->Data.A.IpAddress) + 1);
    bytes[14] = *(reinterpret_cast<unsigned char*>(&pDnsrec->Data.A.IpAddress) + 2);
    bytes[15] = *(reinterpret_cast<unsigned char*>(&pDnsrec->Data.A.IpAddress) + 3);
    
    asio::ip::address_v6 targetAddress(bytes);
    
    g_relays.AddAcceptor(string(server.c_str()), port, targetAddress, true);
    
}


uint16_t g_nextPort = 3677;

void exec_nextport(ac::ACState& ac)
{
    if (ac.words.size() == 2)
    {
        g_nextPort = (uint16_t)atoi(ac.words[1].s.c_str());
    }
    cout << "Next Port: " << g_nextPort << endl;
}


void exec_addrelay(ac::ACState& ac)
{
    addRelay(ac.words[1].s, g_nextPort++);
}

void exec_adddefaultrelays(ac::ACState& ac)
{
    addRelay("gfs262n300.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs204n118.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs208n108.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs214n108.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs270n221.userstorage.mega.co.nz", g_nextPort++);
    addRelay("gfs302n108.userstorage.mega.co.nz", g_nextPort++);
}

void exec_getcode(ac::ACState& ac)
{
    lock_guard g(g_relays.relaycollectionmutex);
    for (auto& ra : g_relays.relayacceptors)
    {
        cout << "pieceUrl = pieceUrl.replace(\"" << ra->reporting_name << "\", \"localhost:" << ra->listen_port << "\");" << endl;
    }
    cout << "pieceUrl = pieceUrl.replace(\"https:\", \"http:\");" << endl;
}

void exec_closeacceptor(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    lock_guard g(g_relays.relaycollectionmutex);
    for (auto& r : g_relays.relayacceptors)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            r->Stop();
            cout << "closed " << r->reporting_name << endl;
        }
    }
}

void exec_closerelay(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    lock_guard g(g_relays.relaycollectionmutex);
    for (auto& r : g_relays.acceptedrelays)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            if (!r->stopped)
            {
                r->StopNow();
                cout << "closed " << r->reporting_name << endl;
            }
        }
    }
}

void exec_pauserelay(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    bool pause = 2 >= ac.words.size() || 0 != atoi(ac.words[2].s.c_str());
    lock_guard g(g_relays.relaycollectionmutex); 
    for (auto& r : g_relays.acceptedrelays)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            if (!r->stopped && r->paused != pause)
            {
                r->Pause(pause);
                cout << (pause ? "paused " : "unpaused ") << r->reporting_name << endl;
            }
        }
    }
}

void exec_relayspeed(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    int speed = atoi(ac.words[2].s.c_str());
    lock_guard g(g_relays.relaycollectionmutex); 
    for (auto& r : g_relays.acceptedrelays)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            r->SetBytesPerSecond(speed);
        }
    }
}

void exec_acceptorspeed(ac::ACState& ac)
{
    bool all = ac.words[1].s == "all";
    regex specific_re(ac.words[1].s);
    int speed = atoi(ac.words[2].s.c_str());
    lock_guard g(g_relays.relaycollectionmutex);
    for (auto& r : g_relays.relayacceptors)
    {
        if (all || std::regex_match(r->reporting_name, specific_re))
        {
            r->SetBytesPerSecond(speed);
        }
    }
}


void exec_report(ac::ACState& ac)
{
    g_relays.report();
}

bool g_exitprogram = false;

void exec_exit(ac::ACState&)
{
    g_exitprogram = true;
}


ac::ACN autocompleteTemplate;

void exec_help(ac::ACState&)
{
    cout << *autocompleteTemplate << flush;
}

ac::ACN autocompleteSyntax()
{
    using namespace autocomplete;
    std::unique_ptr<Either> p(new Either("      "));

    p->Add(exec_acceptorspeed, sequence(text("acceptorspeed"), either(text("all"), param("id")), param("bytespersec")));
    p->Add(exec_relayspeed, sequence(text("relayspeed"), either(text("all"), param("id")), param("bytespersec")));
    p->Add(exec_pauserelay, sequence(text("pauserelay"), either(text("all"), param("id")), opt(either(text("1"), text("0")))));
    p->Add(exec_closerelay, sequence(text("closerelay"), either(text("all"), param("id"))));
    p->Add(exec_closeacceptor, sequence(text("closeacceptor"), either(text("all"), param("id"))));

    p->Add(exec_nextport, sequence(text("nextport"), opt(param("port"))));
    p->Add(exec_addrelay, sequence(text("addrelay"), param("server")));
    p->Add(exec_adddefaultrelays, sequence(text("adddefaultrelays")));
    p->Add(exec_getcode, sequence(text("getcode")));
    p->Add(exec_report, sequence(text("report")));
    p->Add(exec_help, sequence(either(text("help"), text("?"))));
    p->Add(exec_exit, sequence(either(text("exit"))));
    
    return autocompleteTemplate = std::move(p);
}

class MegaCLILogger : public ::mega::Logger {
public:
    virtual void log(const char * /*time*/, int loglevel, const char * /*source*/, const char *message)
    {
#ifdef _WIN32
        OutputDebugStringA(message);
        OutputDebugStringA("\r\n");
#endif

        if (loglevel <= logWarning)
        {
            std::cout << message << std::endl;
        }
    }
};

MegaCLILogger logger;

// local console
Console* console;


int main()
{
#ifdef _WIN32
    SimpleLogger::setLogLevel(logMax);  // warning and stronger to console; info and weaker to VS output window
    SimpleLogger::setOutputClass(&logger);
#else
    SimpleLogger::setAllOutputs(&std::cout);
#endif

    console = new CONSOLE_CLASS;

#ifdef HAVE_AUTOCOMPLETE
    ac::ACN acs = autocompleteSyntax();
#endif
#if defined(WIN32) && defined(NO_READLINE) && defined(HAVE_AUTOCOMPLETE)
    static_cast<WinConsole*>(console)->setAutocompleteSyntax((acs));
#endif


#ifndef NO_READLINE
    char *saved_line = NULL;
    int saved_point = 0;
#ifdef HAVE_AUTOCOMPLETE
    rl_attempted_completion_function = my_rl_completion;
#endif

    rl_save_prompt();

#elif defined(WIN32) && defined(NO_READLINE)
    static_cast<WinConsole*>(console)->setShellConsole(CP_UTF8, GetConsoleOutputCP());
#else
#error non-windows platforms must use the readline library
#endif

    std::thread relayRunnerThread([&]() { g_relays.RunRelays(); });

    while (!g_exitprogram)
    {
#if defined(WIN32) && defined(NO_READLINE)
        static_cast<WinConsole*>(console)->updateInputPrompt("TCPRELAY>");
#else
        rl_callback_handler_install(*dynamicprompt ? dynamicprompt : prompts[COMMAND], store_line);

        // display prompt
        if (saved_line)
        {
            rl_replace_line(saved_line, 0);
            free(saved_line);
        }

        rl_point = saved_point;
        rl_redisplay();
#endif
        char* line = nullptr;
        // command editing loop - exits when a line is submitted or the engine requires the CPU
        for (;;)
        {
            //int w = client->wait();
            Sleep(100);

            //if (w & Waiter::HAVESTDIN)
            {
#if defined(WIN32) && defined(NO_READLINE)
                line = static_cast<WinConsole*>(console)->checkForCompletedInputLine();
#else
                if (prompt == COMMAND)
                {
                    rl_callback_read_char();
                }
                else
                {
                    console->readpwchar(pw_buf, sizeof pw_buf, &pw_buf_pos, &line);
                }
#endif
            }

            //if (w & Waiter::NEEDEXEC || line)
            if (line)
            {
                break;
            }
        }

#ifndef NO_READLINE
        // save line
        saved_point = rl_point;
        saved_line = rl_copy_text(0, rl_end);

        // remove prompt
        rl_save_prompt();
        rl_replace_line("", 0);
        rl_redisplay();
#endif

        if (line)
        {
            // execute user command
            if (*line)
            {
                string consoleOutput;
                if (autoExec(line, strlen(line), autocompleteTemplate, false, consoleOutput, true))
                {
                    if (!consoleOutput.empty())
                    {
                        cout << consoleOutput << endl;
                    }
                }
            }
            free(line);
            line = NULL;

            if (!cerr)
            {
                cerr.clear();
                cerr << "Console error output failed, perhaps on a font related utf8 error or on NULL.  It is now reset." << endl;
            }
            if (!cout)
            {
                cout.clear();
                cerr << "Console output failed, perhaps on a font related utf8 error or on NULL.  It is now reset." << endl;
            }
        }
    }

    g_relays.Stop();
    relayRunnerThread.join();

    return 0;
}
