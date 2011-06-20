/*******************************************************************************
#      ____               __          __  _      _____ _       _               #
#     / __ \              \ \        / / | |    / ____| |     | |              #
#    | |  | |_ __   ___ _ __ \  /\  / /__| |__ | |  __| | ___ | |__   ___      #
#    | |  | | '_ \ / _ \ '_ \ \/  \/ / _ \ '_ \| | |_ | |/ _ \| '_ \ / _ \     #
#    | |__| | |_) |  __/ | | \  /\  /  __/ |_) | |__| | | (_) | |_) |  __/     #
#     \____/| .__/ \___|_| |_|\/  \/ \___|_.__/ \_____|_|\___/|_.__/ \___|     #
#           | |                                                                #
#           |_|                                                                #
#                                                                              #
#                                (c) 2011 by                                   #
#           University of Applied Sciences Northwestern Switzerland            #
#                     Institute of Geomatics Engineering                       #
#                           martin.christen@fhnw.ch                            #
********************************************************************************
*     Licensed under MIT License. Read the file LICENSE for more information   *
*******************************************************************************/
// This is the resample version without mpi intended for regular 
// workstations. Multi cores are supported (OpenMP) and highly recommended.
//------------------------------------------------------------------------------


#include "resample.h"
#include <boost/program_options.hpp>
#include <omp.h>

namespace po = boost::program_options;

int main(int argc, char *argv[])
{
   po::options_description desc("Program-Options");
   desc.add_options()
       ("layer", po::value<std::string>(), "image layer to resample")
       ("numthreads", po::value<int>(), "force number of threads")
       ("verbose", "optional info")
       ;

   po::variables_map vm;


   //---------------------------------------------------------------------------
   // init options:

   boost::shared_ptr<ProcessingSettings> qSettings =  ProcessingUtils::LoadAppSettings();

   if (!qSettings)
   {
      std::cout << "Error in configuration! Check setup.xml\n";
      return ERROR_CONFIG;
   }

   //---------------------------------------------------------------------------
   // create logger
   boost::shared_ptr<Logger> qLogger =  ProcessingUtils::CreateLogger("resample", qSettings);

   if (!qLogger)
   {
      std::cout << "Error in configuration! Check setup.xml\n";
      return ERROR_CONFIG;
   }

   // --------------------------------------------------------------------------
   std::string sLayer;
   bool bError = false;
   bool bVerbose = false;


   try
   {
      po::store(po::parse_command_line(argc, argv, desc), vm);
      po::notify(vm);
   }
   catch (std::exception&)
   {
      bError = true;
   }

   if (!vm.count("layer"))
   {
      bError = true;
   }
   else
   {
      sLayer = vm["layer"].as<std::string>();
   }

   if (vm.count("verbose"))
   {
      bVerbose = true;
   }

   if (vm.count("numthreads"))
   {
      int n = vm["numthreads"].as<int>();
      if (n>0 && n<65)
      {
         std::ostringstream oss; 
         oss << "Forcing number of threads to " << n;
         qLogger->Info(oss.str());
         omp_set_num_threads(n);
      }
   }

   //---------------------------------------------------------------------------
   if (bError)
   {
      qLogger->Error("Wrong parameters!");
      std::ostringstream sstr;
   
      sstr << desc;
      qLogger->Info("\n" + sstr.str());

      return ERROR_PARAMS;
   }

   //---------------------------------------------------------------------------

   std::string sImageLayerDir = FilenameUtils::DelimitPath(qSettings->GetPath()) + sLayer;
   std::string sTileDir = FilenameUtils::DelimitPath(FilenameUtils::DelimitPath(sImageLayerDir) + "tiles");

   boost::shared_ptr<ImageLayerSettings> qImageLayerSettings = ImageLayerSettings::Load(sImageLayerDir);
   if (!qImageLayerSettings)
   {
      qLogger->Error("Failed retrieving image layer settings!");
      return ERROR_IMAGELAYERSETTINGS;
   }

   clock_t t0,t1;
   t0 = clock();

   //--------------------------------------------------------------------------
   // create tile blocks (for each thread)
   TileBlock* pTileBlockArray = _createTileBlockArray();


   //---------------------------------------------------------------------------
   int64 tx0,ty0,tx1,ty1;
   qImageLayerSettings->GetTileExtent(tx0,ty0,tx1,ty1);
   int maxlod = qImageLayerSettings->GetMaxLod();
  

   if (bVerbose)
   {
      std::ostringstream oss;
      oss << "\nResample Setup:\n";
      oss << "     name = " << qImageLayerSettings->GetLayerName() << "\n";
      oss << "   maxlod = " << maxlod << "\n";
      oss << "   extent = " << tx0 << ", " << ty0 << ", " << tx1 << ", " << ty1 << "\n";;
      qLogger->Info(oss.str());
   }

   boost::shared_ptr<MercatorQuadtree> qQuadtree = boost::shared_ptr<MercatorQuadtree>(new MercatorQuadtree());

   std::string qc0 = qQuadtree->TileCoordToQuadkey(tx0, ty0, maxlod);
   std::string qc1 = qQuadtree->TileCoordToQuadkey(tx1, ty1, maxlod);


   for (int nLevelOfDetail = maxlod - 1; nLevelOfDetail>0; nLevelOfDetail--)
   {
      std::ostringstream oss;
      oss << "Processing Level of Detail " << nLevelOfDetail;
      qLogger->Info(oss.str());

      qc0 = StringUtils::Left(qc0, nLevelOfDetail);
      qc1 = StringUtils::Left(qc1, nLevelOfDetail);

      int tmp_lod;
      qQuadtree->QuadKeyToTileCoord(qc0, tx0, ty0, tmp_lod);
      qQuadtree->QuadKeyToTileCoord(qc1, tx1, ty1, tmp_lod);

#     pragma omp parallel for
      for (int64 y=ty0;y<=ty1;y++)
      {
         for (int64 x=tx0;x<=tx1;x++)
         {
            _resampleFromParent(pTileBlockArray, qQuadtree, x, y, nLevelOfDetail, sTileDir);
         }
      }
   }

   // output time to calculate resampling:
   t1=clock();
   std::ostringstream out;
   out << "calculated in: " << double(t1-t0)/double(CLOCKS_PER_SEC) << " s \n";
   qLogger->Info(out.str());

   // clean up
   _destroyTileBlockArray(pTileBlockArray);

}