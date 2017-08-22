//////////////////////////////////////////////////////////////////////////////
/// \file MARLEYGen_module.cc
/// \brief LArSoft interface to the MARLEY (Model of Argon Reaction Low Energy
/// Yields) supernova neutrino event generator
///
/// \author Steven Gardiner <sjgardiner@ucdavis.edu>
//////////////////////////////////////////////////////////////////////////////

// standard library includes
#include <memory>
#include <string>
#include <vector>

// framework includes
#include "art/Framework/Core/EDProducer.h"
#include "art/Framework/Core/ModuleMacros.h"
#include "art/Framework/Principal/Event.h"
#include "art/Framework/Services/Registry/ServiceHandle.h"
#include "art/Framework/Services/Optional/TFileService.h"
#include "art/Framework/Services/Optional/TFileDirectory.h"
#include "fhiclcpp/ParameterSet.h"
#include "fhiclcpp/types/Table.h"
#include "cetlib/exception.h"
#include "messagefacility/MessageLogger/MessageLogger.h"

// art extensions
#include "nutools/RandomUtils/NuRandomService.h"

// LArSoft includes
#include "larcore/Geometry/Geometry.h"
#include "larcoreobj/SummaryData/RunData.h"
#include "nusimdata/SimulationBase/MCTruth.h"
#include "nusimdata/SimulationBase/MCParticle.h"
#include "larsim/EventGenerator/MARLEY/MARLEYHelper.h"
#include "larsim/EventGenerator/MARLEY/ActiveVolumeVertexSampler.h"

// ROOT includes
#include "TFile.h"
#include "TTree.h"

namespace evgen {
  class MarleyGen;
}

class evgen::MarleyGen : public art::EDProducer {

  public:

    using Name = fhicl::Name;
    using Comment = fhicl::Comment;

    /// Collection of configuration parameters for the module
    struct Config {

      fhicl::Table<evgen::ActiveVolumeVertexSampler::Config> vertex_ {
        Name("vertex"),
        Comment("Configuration for selecting the vertex location(s)")
      };

      fhicl::Table<evgen::MARLEYHelper::Config> marley_parameters_ {
        Name("marley_parameters"),
        Comment("Configuration for the MARLEY generator")
      };

      fhicl::Atom<std::string> module_type_ {
        Name("module_type"),
        Comment(""),
        "MARLEYGen" // default value
      };

    }; // struct Config

    // Type to enable FHiCL parameter validation by art
    using Parameters = art::EDProducer::Table<Config>;

    // Configuration-checking constructors
    explicit MarleyGen(const Parameters& p);

    virtual ~MarleyGen();

    virtual void produce(art::Event& e) override;
    virtual void beginRun(art::Run& run) override;

    virtual void reconfigure(const Parameters& p);

  protected:

    // Object that provides an interface to the MARLEY event generator
    std::unique_ptr<evgen::MARLEYHelper> fMarleyHelper;

    // Algorithm that allows us to sample vertex locations within the active
    // volume(s) of the detector
    std::unique_ptr<evgen::ActiveVolumeVertexSampler> fVertexSampler;

    // unique_ptr to the current event created by MARLEY
    std::unique_ptr<marley::Event> fEvent;

    // the MARLEY event TTree
    TTree* fEventTree;

    // Run, subrun, and event numbers from the art::Event being processed
    uint_fast32_t fRunNumber;
    uint_fast32_t fSubRunNumber;
    uint_fast32_t fEventNumber;
};

//------------------------------------------------------------------------------
evgen::MarleyGen::MarleyGen(const Parameters& p)
  : fEvent(new marley::Event), fRunNumber(0), fSubRunNumber(0), fEventNumber(0)
{
  // Configure the module (including MARLEY itself) using the FHiCL parameters
  this->reconfigure(p);

  // Create a ROOT TTree using the TFileService that will store the MARLEY
  // event objects (useful for debugging purposes)
  art::ServiceHandle<art::TFileService> tfs;
  fEventTree = tfs->make<TTree>("MARLEY Event Tree",
    "A tree of marley::Event objects");
  fEventTree->Branch("events", "marley::Event", fEvent.get());

  // Add branches that give the art::Event run, subrun, and event numbers for
  // easy match-ups between the MARLEY and art TTrees. All three are recorded
  // as 32-bit unsigned integers.
  fEventTree->Branch("run_number", &fRunNumber, "run_number/i");
  fEventTree->Branch("subrun_number", &fSubRunNumber, "subrun_number/i");
  fEventTree->Branch("event_number", &fEventNumber, "event_number/i");

  produces< std::vector<simb::MCTruth>   >();
  produces< sumdata::RunData, art::InRun >();
}

//------------------------------------------------------------------------------
evgen::MarleyGen::~MarleyGen()
{
}

//------------------------------------------------------------------------------
void evgen::MarleyGen::beginRun(art::Run& run)
{
  // grab the geometry object to see what geometry we are using
  art::ServiceHandle<geo::Geometry> geo;
  std::unique_ptr<sumdata::RunData>
    runcol(new sumdata::RunData(geo->DetectorName()));

  run.put(std::move(runcol));
}

//------------------------------------------------------------------------------
void evgen::MarleyGen::produce(art::Event& e)
{
  // Get the run, subrun, and event numbers from the current art::Event
  fRunNumber = e.run();
  fSubRunNumber = e.subRun();
  fEventNumber = e.event();

  std::unique_ptr< std::vector<simb::MCTruth> >
    truthcol(new std::vector<simb::MCTruth>);

  // Get the primary vertex location for this event
  art::ServiceHandle<geo::Geometry> geo;
  TLorentzVector vertex_pos = fVertexSampler->sample_vertex_pos(*geo);

  // Create the MCTruth object, and retrieve the marley::Event object
  // that was generated as it was created
  simb::MCTruth truth = fMarleyHelper->create_MCTruth(vertex_pos,
    fEvent.get());

  // Write the marley::Event object to the event tree
  fEventTree->Fill();

  truthcol->push_back(truth);

  e.put(std::move(truthcol));
}

//------------------------------------------------------------------------------
void evgen::MarleyGen::reconfigure(const Parameters& p)
{
  const auto& seed_service = art::ServiceHandle<rndm::NuRandomService>();
  const auto& geom_service = art::ServiceHandle<geo::Geometry>();

  // Create a new evgen::ActiveVolumeVertexSampler object based on the current
  // configuration
  fVertexSampler = std::make_unique<evgen::ActiveVolumeVertexSampler>(
    p().vertex_, *seed_service, *geom_service, "MARLEY_Vertex_Sampler");

  // Create a new marley::Generator object based on the current configuration
  fMarleyHelper = std::make_unique<MARLEYHelper>(p().marley_parameters_,
    *seed_service, "MARLEY");
}

DEFINE_ART_MODULE(evgen::MarleyGen)
