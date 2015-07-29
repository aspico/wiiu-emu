#include <pugixml.hpp>
#include <docopt.h>
#include "bitutils.h"
#include "codetests.h"
#include "filesystem.h"
#include "instructiondata.h"
#include "interpreter.h"
#include "processor.h"
#include "jit.h"
#include "loader.h"
#include "log.h"
#include "memory.h"
#include "modules/coreinit/coreinit.h"
#include "modules/coreinit/coreinit_memheap.h"
#include "modules/erreula/erreula.h"
#include "modules/gx2/gx2.h"
#include "modules/nn_ac/nn_ac.h"
#include "modules/nn_act/nn_act.h"
#include "modules/nn_fp/nn_fp.h"
#include "modules/nn_save/nn_save.h"
#include "modules/zlib125/zlib125.h"
#include "modules/proc_ui/proc_ui.h"
#include "modules/padscore/padscore.h"
#include "system.h"
#include "usermodule.h"

std::shared_ptr<spdlog::logger>
gLog;

void initialiseEmulator();
bool test(const std::string &as, const std::string &path);
bool play(const std::string &path);

static const char USAGE[] =
   R"(WiiU Emulator

       Usage:
         wiiu play [--jit | --jitdebug] [--logfile] [--log-async] [--log-level=<log-level>] <game directory>
         wiiu test [--jit | --jitdebug] [--logfile] [--log-async] [--log-level=<log-level>] [--as=<ppcas>] <test directory>
         wiiu (-h | --help)
         wiiu --version

       Options:
         -h --help     Show this screen.
         --version     Show version.
         --jit         Enables the JIT engine.
         --logfile     Redirect log output to file.
         --log-async   Enable asynchronous logging.
         --log-level=<log-level> [default: trace]
                       Only display logs with severity equal to or greater than this level.
                       Available levels: trace, debug, info, notice, warning, error, critical, alert, emerg, off
         --as=<ppcas>  Path to PowerPC assembler [default: powerpc-eabi-as.exe].
   )";

static std::string
getGameName(const std::string &directory)
{
   auto parent = std::experimental::filesystem::path(directory).parent_path().generic_string();
   return directory.substr(parent.size() + 1);
}

int main(int argc, char **argv)
{
   auto args = docopt::docopt(USAGE, { argv + 1, argv + argc }, true, "WiiU 0.1");
   bool result = false;

   if (args["--jitdebug"].asBool()) {
      gInterpreter.setJitMode(InterpJitMode::Debug);
   } else if (args["--jit"].asBool()) {
      gInterpreter.setJitMode(InterpJitMode::Enabled);
   } else {
      gInterpreter.setJitMode(InterpJitMode::Disabled);
   }

   // Create the logger
   std::vector<spdlog::sink_ptr> sinks;
   sinks.push_back(std::make_shared<spdlog::sinks::stdout_sink_st>());

   if (args["--logfile"].asBool()) {
      std::string file;

      if (args["play"].asBool()) {
         file = getGameName(args["<game directory>"].asString());
      } else if (args["test"].asBool()) {
         file = "tests";
      }

      sinks.push_back(std::make_shared<spdlog::sinks::daily_file_sink_st>(file, "txt", 23, 59));
   }

   if (args["--log-async"].asBool()) {
      spdlog::set_async_mode(0x100000);
   }

   gLog = std::make_shared<spdlog::logger>("logger", begin(sinks), end(sinks));
   gLog->set_level(spdlog::level::trace);

   auto log_level = args["--log-level"].isString()? args["--log-level"].asString(): "trace";
   for (int l = spdlog::level::trace; l <= spdlog::level::off; l++) {
      if (spdlog::level::to_str((spdlog::level::level_enum) l) == log_level) {
         gLog->set_level((spdlog::level::level_enum) l);
         break;
      }
   }

   initialiseEmulator();

   if (args["play"].asBool()) {
      gLog->set_pattern("[%l:%t] %v");
      result = play(args["<game directory>"].asString());
   } else if (args["test"].asBool()) {
      gLog->set_pattern("%v");
      result = test(args["--as"].asString(), args["<test directory>"].asString());
   }

   system("PAUSE");
   return result ? 0 : -1;
}

static void
initialiseEmulator()
{
   Interpreter::RegisterFunctions();
   JitManager::RegisterFunctions();

   // Kernel modules
   CoreInit::RegisterFunctions();
   ErrEula::RegisterFunctions();
   GX2::RegisterFunctions();
   NNAc::RegisterFunctions();
   NNAct::RegisterFunctions();
   NNFp::RegisterFunctions();
   NNSave::RegisterFunctions();
   PadScore::RegisterFunctions();
   ProcUI::RegisterFunctions();
   Zlib125::RegisterFunctions();

   // Initialise emulator systems
   gMemory.initialise();
   gInstructionTable.initialise();
   gJitManager.initialise();
   gSystem.registerModule("coreinit", new CoreInit {});
   gSystem.registerModule("erreula.rpl", new ErrEula {});
   gSystem.registerModule("gx2", new GX2 {});
   gSystem.registerModule("nn_ac", new NNAc {});
   gSystem.registerModule("nn_act", new NNAct {});
   gSystem.registerModule("nn_fp", new NNFp {});
   gSystem.registerModule("nn_save", new NNSave {});
   gSystem.registerModule("padscore", new PadScore {});
   gSystem.registerModule("proc_ui", new ProcUI {});
   gSystem.registerModule("zlib125", new Zlib125 {});
   gSystem.initialiseModules();
}

static bool
test(const std::string &as, const std::string &path)
{
   return executeCodeTests(as, path);
}

static bool
play(const std::string &path)
{
   // Setup filesystem
   VirtualFileSystem fs { "/" };
   fs.mount("/vol", std::make_unique<HostFileSystem>(path + "/data"));
   gSystem.setFileSystem(&fs);

   // Read cos.xml
   pugi::xml_document doc;
   auto cosFile = fs.openFile("/vol/code/cos.xml", FileSystem::Input | FileSystem::Binary);

   if (!cosFile) {
      gLog->error("Error opening /vol/code/cos.xml");
      return false;
   }

   auto size = cosFile->size();
   auto buffer = std::vector<char>(size);
   cosFile->read(buffer.data(), size);

   // Parse cos.xml
   auto parseResult = doc.load_buffer_inplace(buffer.data(), buffer.size());

   if (!parseResult) {
      gLog->error("Error parsing /vol/code/cos.xml");
      return false;
   }

   auto rpxPath = doc.child("app").child("argstr").child_value();

   // Read rpx file
   auto rpxFile = fs.openFile(std::string("/vol/code/") + rpxPath, FileSystem::Input | FileSystem::Binary);

   if (!rpxFile) {
      gLog->error("Error opening /vol/code/{}", rpxPath);
      return false;
   }

   buffer.resize(rpxFile->size());
   rpxFile->read(buffer.data(), buffer.size());

   // Load the rpl into memory
   Loader loader;
   UserModule module;
   gSystem.registerModule(rpxPath, &module);

   if (!loader.loadRPL(module, buffer.data(), buffer.size())) {
      gLog->error("Could not load {}", rpxPath);
      return false;
   }

   gLog->debug("Succesfully loaded {}", path);
   auto entryPoint = module.entryPoint;
   auto stackSize = module.defaultStackSize;

   // Initialise default heaps
   // TODO: Call __preinit_user
   CoreInitDefaultHeap();

   // Startup processor
   gProcessor.start();

   // Allocate OSThread structures and stacks
   auto thread1 = OSAllocFromSystem<OSThread>();
   auto stack1 = reinterpret_cast<uint8_t*>(OSAllocFromSystem(stackSize, 8));
   auto stack1top = stack1 + stackSize;

   auto thread0 = OSAllocFromSystem<OSThread>();
   auto stack0 = reinterpret_cast<uint8_t*>(OSAllocFromSystem(stackSize, 8));
   auto stack0top = stack0 + stackSize;

   auto thread2 = OSAllocFromSystem<OSThread>();
   auto stack2 = reinterpret_cast<uint8_t*>(OSAllocFromSystem(stackSize, 8));
   auto stack2top = stack0 + stackSize;

   // Create the idle threads for core0 and core2
   OSCreateThread(thread0, 0, 0, nullptr, stack0top, stackSize, 16, OSThreadAttributes::AffinityCPU0);
   OSCreateThread(thread2, 0, 0, nullptr, stack2top, stackSize, 16, OSThreadAttributes::AffinityCPU2);

   // Set the two idle threads to inactive
   thread0->state = OSThreadState::None;
   thread2->state = OSThreadState::None;

   // Create the main thread for core1
   OSCreateThread(thread1, entryPoint, 0, nullptr, stack1top, stackSize, 16, OSThreadAttributes::AffinityCPU1);

   // Set the default threads
   OSSetDefaultThread(0, thread0);
   OSSetDefaultThread(1, thread1);
   OSSetDefaultThread(2, thread2);

   // Start thread 1
   OSResumeThread(thread1);

   // Wait for all processor threads to exit
   gProcessor.join();

   // Free allocated data
   OSFreeToSystem(thread0);
   OSFreeToSystem(thread1);
   OSFreeToSystem(thread2);
   OSFreeToSystem(stack0);
   OSFreeToSystem(stack1);
   OSFreeToSystem(stack2);

   return true;
}
