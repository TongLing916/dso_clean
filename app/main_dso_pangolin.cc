#include <locale.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <thread>

#include <glog/logging.h>
#include <boost/thread.hpp>

#include "full_system/full_system.h"
#include "full_system/pixel_selector2.h"
#include "io_wrapper/image_display.h"
#include "io_wrapper/output_3d_wrapper.h"
#include "io_wrapper/output_wrapper/sample_output_wrapper.h"
#include "io_wrapper/pangolin/pangolin_dso_viewer.h"
#include "optimization_backend/accumulators/matrix_accumulators.h"
#include "util/dataset_reader.h"
#include "util/global_calib.h"
#include "util/global_funcs.h"
#include "util/num_type.h"
#include "util/settings.h"

std::string vignette = "";
std::string gammaCalib = "";
std::string source = "";
std::string calib = "";
double rescale = 1;
bool reverse = false;
bool disableROS = false;
int start = 0;
int end = 100000;
bool prefetch = false;
float playbackSpeed = 0;  // 0 for linearize (play as fast as possible, while
                          // sequentializing tracking & mapping). otherwise,
                          // factor on timestamps.
bool preload = false;
bool useSampleOutput = false;

int mode = 0;

bool firstRosSpin = false;

using namespace dso;

void my_exit_handler(int s) {
  printf("Caught signal %d\n", s);
  exit(1);
}

void exitThread() {
  struct sigaction sigIntHandler;
  sigIntHandler.sa_handler = my_exit_handler;
  sigemptyset(&sigIntHandler.sa_mask);
  sigIntHandler.sa_flags = 0;
  sigaction(SIGINT, &sigIntHandler, NULL);

  firstRosSpin = true;
  while (true) pause();
}

void settingsDefault(int preset) {
  printf("\n=============== PRESET Settings: ===============\n");
  if (preset == 0 || preset == 1) {
    printf(
        "DEFAULT settings:\n"
        "- %s real-time enforcing\n"
        "- 2000 active points\n"
        "- 5-7 active frames\n"
        "- 1-6 LM iteration each KF\n"
        "- original image resolution\n",
        preset == 0 ? "no " : "1x");

    playbackSpeed = (preset == 0 ? 0 : 1);
    preload = preset == 1;
    setting_desiredImmatureDensity = 1500;
    setting_desiredPointDensity = 2000;
    setting_minFrames = 5;
    setting_maxFrames = 7;
    setting_maxOptIterations = 6;
    setting_minOptIterations = 1;

    setting_logStuff = false;
  }

  if (preset == 2 || preset == 3) {
    printf(
        "FAST settings:\n"
        "- %s real-time enforcing\n"
        "- 800 active points\n"
        "- 4-6 active frames\n"
        "- 1-4 LM iteration each KF\n"
        "- 424 x 320 image resolution\n",
        preset == 0 ? "no " : "5x");

    playbackSpeed = (preset == 2 ? 0 : 5);
    preload = preset == 3;
    setting_desiredImmatureDensity = 600;
    setting_desiredPointDensity = 800;
    setting_minFrames = 4;
    setting_maxFrames = 6;
    setting_maxOptIterations = 4;
    setting_minOptIterations = 1;

    benchmarkSetting_width = 424;
    benchmarkSetting_height = 320;

    setting_logStuff = false;
  }

  printf("==============================================\n");
}

void parseArgument(char* arg) {
  int option;
  float foption;
  char buf[1000];

  if (1 == sscanf(arg, "sampleoutput=%d", &option)) {
    if (option == 1) {
      useSampleOutput = true;
      printf("USING SAMPLE OUTPUT WRAPPER!\n");
    }
    return;
  }

  if (1 == sscanf(arg, "quiet=%d", &option)) {
    if (option == 1) {
      setting_debugout_runquiet = true;
      printf("QUIET MODE, I'll shut up!\n");
    }
    return;
  }

  if (1 == sscanf(arg, "preset=%d", &option)) {
    settingsDefault(option);
    return;
  }

  if (1 == sscanf(arg, "rec=%d", &option)) {
    if (option == 0) {
      disableReconfigure = true;
      printf("DISABLE RECONFIGURE!\n");
    }
    return;
  }

  if (1 == sscanf(arg, "noros=%d", &option)) {
    if (option == 1) {
      disableROS = true;
      disableReconfigure = true;
      printf("DISABLE ROS (AND RECONFIGURE)!\n");
    }
    return;
  }

  if (1 == sscanf(arg, "nolog=%d", &option)) {
    if (option == 1) {
      setting_logStuff = false;
      printf("DISABLE LOGGING!\n");
    }
    return;
  }
  if (1 == sscanf(arg, "reverse=%d", &option)) {
    if (option == 1) {
      reverse = true;
      printf("REVERSE!\n");
    }
    return;
  }
  if (1 == sscanf(arg, "nogui=%d", &option)) {
    if (option == 1) {
      disableAllDisplay = true;
      printf("NO GUI!\n");
    }
    return;
  }
  if (1 == sscanf(arg, "nomt=%d", &option)) {
    if (option == 1) {
      multiThreading = false;
      printf("NO MultiThreading!\n");
    }
    return;
  }
  if (1 == sscanf(arg, "prefetch=%d", &option)) {
    if (option == 1) {
      prefetch = true;
      printf("PREFETCH!\n");
    }
    return;
  }
  if (1 == sscanf(arg, "start=%d", &option)) {
    start = option;
    printf("START AT %d!\n", start);
    return;
  }
  if (1 == sscanf(arg, "end=%d", &option)) {
    end = option;
    printf("END AT %d!\n", start);
    return;
  }

  if (1 == sscanf(arg, "files=%s", buf)) {
    source = buf;
    printf("loading data from %s!\n", source.c_str());
    return;
  }

  if (1 == sscanf(arg, "calib=%s", buf)) {
    calib = buf;
    printf("loading calibration from %s!\n", calib.c_str());
    return;
  }

  if (1 == sscanf(arg, "vignette=%s", buf)) {
    vignette = buf;
    printf("loading vignette from %s!\n", vignette.c_str());
    return;
  }

  if (1 == sscanf(arg, "gamma=%s", buf)) {
    gammaCalib = buf;
    printf("loading gammaCalib from %s!\n", gammaCalib.c_str());
    return;
  }

  if (1 == sscanf(arg, "rescale=%f", &foption)) {
    rescale = foption;
    printf("RESCALE %f!\n", rescale);
    return;
  }

  if (1 == sscanf(arg, "speed=%f", &foption)) {
    playbackSpeed = foption;
    printf("PLAYBACK SPEED %f!\n", playbackSpeed);
    return;
  }

  if (1 == sscanf(arg, "save=%d", &option)) {
    if (option == 1) {
      debugSaveImages = true;
      if (42 == system("rm -rf images_out"))
        printf(
            "system call returned 42 - what are the odds?. This is only here "
            "to shut up the compiler.\n");
      if (42 == system("mkdir images_out"))
        printf(
            "system call returned 42 - what are the odds?. This is only here "
            "to shut up the compiler.\n");
      if (42 == system("rm -rf images_out"))
        printf(
            "system call returned 42 - what are the odds?. This is only here "
            "to shut up the compiler.\n");
      if (42 == system("mkdir images_out"))
        printf(
            "system call returned 42 - what are the odds?. This is only here "
            "to shut up the compiler.\n");
      printf("SAVE IMAGES!\n");
    }
    return;
  }

  if (1 == sscanf(arg, "mode=%d", &option)) {
    mode = option;
    if (option == 0) {
      printf("PHOTOMETRIC MODE WITH CALIBRATION!\n");
    }
    if (option == 1) {
      printf("PHOTOMETRIC MODE WITHOUT CALIBRATION!\n");
      setting_photometricCalibration = 0;
      setting_affineOptModeA =
          0;  //-1: fix. >=0: optimize (with prior, if > 0).
      setting_affineOptModeB =
          0;  //-1: fix. >=0: optimize (with prior, if > 0).
    }
    if (option == 2) {
      printf("PHOTOMETRIC MODE WITH PERFECT IMAGES!\n");
      setting_photometricCalibration = 0;
      setting_affineOptModeA =
          -1;  //-1: fix. >=0: optimize (with prior, if > 0).
      setting_affineOptModeB =
          -1;  //-1: fix. >=0: optimize (with prior, if > 0).
      setting_minGradHistAdd = 3;
    }
    return;
  }

  printf("could not parse argument \"%s\"!!!!\n", arg);
}

int main(int argc, char** argv) {
  // setlocale(LC_ALL, "");
  for (int i = 1; i < argc; ++i) parseArgument(argv[i]);

  // hook crtl+C.
  boost::thread exThread = boost::thread(exitThread);

  DatasetReader* reader =
      new DatasetReader(source, calib, gammaCalib, vignette);
  reader->SetGlobalCalibration();

  if (setting_photometricCalibration > 0 &&
      reader->GetPhotometricGamma() == 0) {
    printf(
        "ERROR: dont't have photometric calibation. Need to use commandline "
        "options mode=1 or mode=2 ");
    exit(1);
  }

  const int num_of_images = static_cast<int>(reader->GetNumImages());
  int lstart = start;
  int lend = end;
  int linc = 1;
  if (reverse) {
    printf("REVERSE!!!!");
    lstart = end - 1;
    if (lstart >= num_of_images) {
      lstart = num_of_images - 1;
    }
    lend = start;
    linc = -1;
  }

  FullSystem* fullSystem = new FullSystem();
  fullSystem->setGammaFunction(reader->GetPhotometricGamma());
  fullSystem->linearizeOperation = (playbackSpeed == 0);

  IOWrap::PangolinDSOViewer* viewer = 0;
  if (!disableAllDisplay) {
    viewer = new IOWrap::PangolinDSOViewer(wG[0], hG[0], false);
    fullSystem->outputWrapper.emplace_back(viewer);
  }

  if (useSampleOutput)
    fullSystem->outputWrapper.emplace_back(new IOWrap::SampleOutputWrapper());

  // to make MacOS happy: run this in dedicated thread -- and use this one to
  // run the GUI.
  std::thread runthread([&]() {
    std::vector<int> idsToPlay;
    std::vector<double> timesToPlayAt;
    for (int i = lstart; i >= 0 && i < num_of_images && linc * i < linc * lend;
         i += linc) {
      idsToPlay.emplace_back(i);
      if (timesToPlayAt.size() == 0) {
        timesToPlayAt.emplace_back((double)0);
      } else {
        double tsThis = reader->GetTimestamp(idsToPlay[idsToPlay.size() - 1]);
        double tsPrev = reader->GetTimestamp(idsToPlay[idsToPlay.size() - 2]);
        timesToPlayAt.emplace_back(timesToPlayAt.back() +
                                   fabs(tsThis - tsPrev) / playbackSpeed);
      }
    }

    std::vector<ImageAndExposure*> preloadedImages;
    if (preload) {
      printf("LOADING ALL IMAGES!\n");
      for (int ii = 0; ii < (int)idsToPlay.size(); ++ii) {
        int i = idsToPlay[ii];
        preloadedImages.emplace_back(reader->GetImage(i));
      }
    }

    struct timeval tv_start;
    gettimeofday(&tv_start, NULL);
    clock_t started = clock();
    double sInitializerOffset = 0;

    for (int ii = 0; ii < (int)idsToPlay.size(); ++ii) {
      if (!fullSystem->initialized)  // if not initialized: reset start time.
      {
        gettimeofday(&tv_start, NULL);
        started = clock();
        sInitializerOffset = timesToPlayAt[ii];
      }

      int i = idsToPlay[ii];

      ImageAndExposure* img;
      if (preload)
        img = preloadedImages[ii];
      else
        img = reader->GetImage(i);

      bool skipFrame = false;
      if (playbackSpeed != 0) {
        struct timeval tv_now;
        gettimeofday(&tv_now, NULL);
        double sSinceStart =
            sInitializerOffset +
            ((tv_now.tv_sec - tv_start.tv_sec) +
             (tv_now.tv_usec - tv_start.tv_usec) / (1000.0f * 1000.0f));

        if (sSinceStart < timesToPlayAt[ii])
          usleep((int)((timesToPlayAt[ii] - sSinceStart) * 1000 * 1000));
        else if (sSinceStart > timesToPlayAt[ii] + 0.5 + 0.1 * (ii % 2)) {
          printf("SKIPFRAME %d (play at %f, now it is %f)!\n", ii,
                 timesToPlayAt[ii], sSinceStart);
          skipFrame = true;
        }
      }

      if (!skipFrame) fullSystem->addActiveFrame(img, i);

      delete img;

      if (fullSystem->initFailed || setting_fullResetRequested) {
        if (ii < 250 || setting_fullResetRequested) {
          printf("RESETTING!\n");

          std::vector<IOWrap::Output3DWrapper*> wraps =
              fullSystem->outputWrapper;
          delete fullSystem;

          for (IOWrap::Output3DWrapper* ow : wraps) ow->reset();

          fullSystem = new FullSystem();
          fullSystem->setGammaFunction(reader->GetPhotometricGamma());
          fullSystem->linearizeOperation = (playbackSpeed == 0);

          fullSystem->outputWrapper = wraps;

          setting_fullResetRequested = false;
        }
      }

      if (fullSystem->isLost) {
        printf("LOST!!\n");
        break;
      }
    }
    fullSystem->blockUntilMappingIsFinished();
    clock_t ended = clock();
    struct timeval tv_end;
    gettimeofday(&tv_end, NULL);

    fullSystem->printResult("result.txt");

    int numFramesProcessed = abs(idsToPlay[0] - idsToPlay.back());
    double numSecondsProcessed = fabs(reader->GetTimestamp(idsToPlay[0]) -
                                      reader->GetTimestamp(idsToPlay.back()));
    double MilliSecondsTakenSingle =
        1000.0f * (ended - started) / (float)(CLOCKS_PER_SEC);
    double MilliSecondsTakenMT =
        sInitializerOffset + ((tv_end.tv_sec - tv_start.tv_sec) * 1000.0f +
                              (tv_end.tv_usec - tv_start.tv_usec) / 1000.0f);
    printf(
        "\n======================"
        "\n%d Frames (%.1f fps)"
        "\n%.2fms per frame (single core); "
        "\n%.2fms per frame (multi core); "
        "\n%.3fx (single core); "
        "\n%.3fx (multi core); "
        "\n======================\n\n",
        numFramesProcessed, numFramesProcessed / numSecondsProcessed,
        MilliSecondsTakenSingle / numFramesProcessed,
        MilliSecondsTakenMT / (float)numFramesProcessed,
        1000 / (MilliSecondsTakenSingle / numSecondsProcessed),
        1000 / (MilliSecondsTakenMT / numSecondsProcessed));
    // fullSystem->printFrameLifetimes();
    if (setting_logStuff) {
      std::ofstream tmlog;
      tmlog.open("logs/time.txt", std::ios::trunc | std::ios::out);
      tmlog << 1000.0f * (ended - started) /
                   (float)(CLOCKS_PER_SEC * num_of_images)
            << " "
            << ((tv_end.tv_sec - tv_start.tv_sec) * 1000.0f +
                (tv_end.tv_usec - tv_start.tv_usec) / 1000.0f) /
                   static_cast<float>(num_of_images)
            << "\n";
      tmlog.flush();
      tmlog.close();
    }

  });

  if (viewer != 0) viewer->run();

  runthread.join();

  for (IOWrap::Output3DWrapper* ow : fullSystem->outputWrapper) {
    ow->join();
    delete ow;
  }

  printf("DELETE FULLSYSTEM!\n");
  delete fullSystem;

  printf("DELETE READER!\n");
  delete reader;

  printf("EXIT NOW!\n");
  return 0;
}
