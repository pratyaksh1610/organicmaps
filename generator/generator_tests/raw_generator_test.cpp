#include "testing/testing.hpp"

#include "generator/generator_tests_support/test_generator.hpp"
#include "generator/descriptions_section_builder.hpp"

#include "search/cities_boundaries_table.hpp"

#include "routing/index_graph_loader.hpp"
#include "routing/maxspeeds.hpp"

#include "indexer/classificator.hpp"
#include "indexer/feature_algo.hpp"
#include "indexer/ftypes_matcher.hpp"

namespace raw_generator_tests
{
using TestRawGenerator = generator::tests_support::TestRawGenerator;

uint32_t GetFeatureType(FeatureType & ft)
{
  uint32_t res = 0;
  ft.ForEachType([&res](uint32_t t)
  {
    TEST_EQUAL(res, 0, ());
    res = t;
  });
  return res;
}

// https://github.com/organicmaps/organicmaps/issues/2035
UNIT_CLASS_TEST(TestRawGenerator, Towns)
{
  uint32_t const cityType = classif().GetTypeByPath({"place", "city"});
  uint32_t const townType = classif().GetTypeByPath({"place", "town"});
  uint32_t const villageType = classif().GetTypeByPath({"place", "village"});

  std::string const mwmName = "Towns";
  std::string const mwmWorld = WORLD_FILE_NAME;
  BuildFB("./data/osm_test_data/towns.osm", mwmName, true /* makeWorld */);

  size_t count = 0;
  ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
  {
    ++count;
    //LOG(LINFO, (fb));

    TEST(!fb.HasType(cityType), ());

    bool const isTown = (fb.GetName() == "El Dorado");
    TEST_EQUAL(isTown, fb.HasType(townType), ());
    TEST_NOT_EQUAL(isTown, fb.HasType(villageType), ());

    TEST(fb.GetRank() > 0, ());
  });
  TEST_EQUAL(count, 4, ());

  count = 0;
  ForEachFB(mwmWorld, [&](feature::FeatureBuilder const & fb)
  {
    ++count;
    TEST(!fb.HasType(villageType), ());

    bool const isTown = (fb.GetName() == "El Dorado");
    TEST(fb.HasType(isTown ? townType : cityType), ());
  });
  TEST_EQUAL(count, 1, ());

  // Prepare features data source.
  FrozenDataSource dataSource;
  std::vector<MwmSet::MwmId> mwmIDs;
  for (auto const & name : { mwmName, mwmWorld })
  {
    BuildFeatures(name);
    BuildSearch(name);

    auto const res = dataSource.RegisterMap(platform::LocalCountryFile::MakeTemporary(GetMwmPath(name)));
    TEST_EQUAL(res.second, MwmSet::RegResult::Success, ());
    mwmIDs.push_back(std::move(res.first));
  }

  // Load boundaries.
  search::CitiesBoundariesTable table(dataSource);
  TEST(table.Load(), ());
  TEST_EQUAL(table.GetSize(), 1, ());

  // Iterate for features in World.
  count = 0;
  FeaturesLoaderGuard guard(dataSource, mwmIDs[1]);
  size_t const numFeatures = guard.GetNumFeatures();
  for (size_t id = 0; id < numFeatures; ++id)
  {
    auto ft = guard.GetFeatureByIndex(id);

    std::string_view const name = ft->GetName(StringUtf8Multilang::kDefaultCode);
    if (!name.empty())
    {
      TEST_EQUAL(ft->GetGeomType(), feature::GeomType::Point, ());

      search::CitiesBoundariesTable::Boundaries boundary;
      TEST(table.Get(id, boundary), ());
      TEST(boundary.HasPoint(ft->GetCenter()), ());

      bool const isTown = (name == "El Dorado");
      TEST_EQUAL(GetFeatureType(*ft), isTown ? townType : cityType, ());

      ++count;
    }
  }

  TEST_EQUAL(count, 1, ());
}

// https://github.com/organicmaps/organicmaps/issues/2475
UNIT_CLASS_TEST(TestRawGenerator, HighwayLinks)
{
  std::string const mwmName = "Highways";
  BuildFB("./data/osm_test_data/highway_links.osm", mwmName);

  BuildFeatures(mwmName);
  BuildRouting(mwmName, "Spain");

  auto const fid2osm = LoadFID2OsmID(mwmName);

  using namespace routing;
  MaxspeedType from120 = 104; // like SpeedMacro::Speed104KmPH
  std::unordered_map<uint64_t, uint16_t> osmID2Speed = {
    { 23011515, from120 }, { 23011492, from120 }, { 10689329, from120 }, { 371581901, from120 },
    { 1017695671, from120 }, { 577365212, from120 }, { 23011612, from120 }, { 1017695670, from120 },
    { 304871606, from120 }, { 1017695669, from120 }, { 577365213, from120 }, { 369541035, from120 },
    { 1014336646, from120 }, { 466365947, from120 }, { 23011511, from120 }
  };
  /// @todo Actually, better to assign speed for this way too.
  std::unordered_set<uint64_t> osmNoSpeed = { 23691193, 1017695668 };

  FrozenDataSource dataSource;
  auto const res = dataSource.RegisterMap(platform::LocalCountryFile::MakeTemporary(GetMwmPath(mwmName)));
  CHECK_EQUAL(res.second, MwmSet::RegResult::Success, ());

  FeaturesLoaderGuard guard(dataSource, res.first);

  auto const speeds = routing::LoadMaxspeeds(guard.GetHandle());
  CHECK(speeds, ());

  size_t speedChecked = 0, noSpeed = 0;

  uint32_t const count = guard.GetNumFeatures();
  for (uint32_t id = 0; id < count; ++id)
  {
    auto const iOsmID = fid2osm.find(id);
    if (iOsmID == fid2osm.end())
      continue;
    auto const osmID = iOsmID->second.GetSerialId();

    auto const iSpeed = osmID2Speed.find(osmID);
    if (iSpeed != osmID2Speed.end())
    {
      ++speedChecked;
      auto const speed = speeds->GetMaxspeed(id);
      TEST(speed.IsValid(), ());
      TEST_EQUAL(speed.GetForward(), iSpeed->second, ());
    }

    auto const iNoSpeed = osmNoSpeed.find(osmID);
    if (iNoSpeed != osmNoSpeed.end())
    {
      ++noSpeed;
      TEST(!speeds->GetMaxspeed(id).IsValid(), ());
    }
  }

  TEST_EQUAL(speedChecked, osmID2Speed.size(), ());
  TEST_EQUAL(noSpeed, osmNoSpeed.size(), ());
}

UNIT_CLASS_TEST(TestRawGenerator, Building3D)
{
  auto const & buildingChecker = ftypes::IsBuildingChecker::Instance();
  auto const & buildingPartChecker = ftypes::IsBuildingPartChecker::Instance();
  auto const & buildingHasPartsChecker = ftypes::IsBuildingHasPartsChecker::Instance();

  std::string const mwmName = "Building3D";
  BuildFB("./data/osm_test_data/building3D.osm", mwmName);

  size_t buildings = 0, buildingParts = 0, buildingHasParts = 0;
  ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
  {
    auto const & types = fb.GetTypes();
    if (buildingChecker(types))
      ++buildings;
    if (buildingPartChecker(types))
      ++buildingParts;
    if (buildingHasPartsChecker(types))
      ++buildingHasParts;
  });

  TEST_EQUAL(buildings, 1, ());
  TEST_GREATER(buildingParts, 0, ());
  TEST_EQUAL(buildingHasParts, 1, ());
}

// https://www.openstreetmap.org/relation/13430355
UNIT_CLASS_TEST(TestRawGenerator, BuildingRelation)
{
  auto const & buildingChecker = ftypes::IsBuildingChecker::Instance();
  auto const & buildingPartChecker = ftypes::IsBuildingPartChecker::Instance();
  auto const & buildingHasPartsChecker = ftypes::IsBuildingHasPartsChecker::Instance();

  std::string const mwmName = "Building";
  BuildFB("./data/osm_test_data/building_relation.osm", mwmName);

  {
    size_t buildings = 0, buildingParts = 0, buildingHasParts = 0;
    ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
    {
      auto const & types = fb.GetTypes();
      if (buildingChecker(types))
        ++buildings;
      if (buildingPartChecker(types))
        ++buildingParts;
      if (buildingHasPartsChecker(types))
        ++buildingHasParts;
    });

    /// @todo Should be 1, 3, 1 when will implement one FB with multiple polygons.
    TEST_EQUAL(buildings, 2, ());
    TEST_EQUAL(buildingParts, 3, ());
    TEST_EQUAL(buildingHasParts, 2, ());
  }

  BuildFeatures(mwmName);

  size_t features = 0;
  double buildings = 0, buildingParts = 0;
  ForEachFeature(mwmName, [&](std::unique_ptr<FeatureType> ft)
  {
    if (ft->GetGeomType() != feature::GeomType::Area)
      return;

    feature::TypesHolder types(*ft);
    if (buildingChecker(types))
      buildings += feature::CalcArea(*ft);
    else if (buildingPartChecker(types))
      buildingParts += feature::CalcArea(*ft);

    ++features;
  });

  TEST_EQUAL(features, 5, ());
  TEST_ALMOST_EQUAL_ABS(buildings, buildingParts, 1.0E-4, ());
}

UNIT_CLASS_TEST(TestRawGenerator, AreaHighway)
{
  std::string const mwmName = "AreaHighway";
  BuildFB("./data/osm_test_data/highway_area.osm", mwmName);

  uint32_t const waterType = classif().GetTypeByPath({"natural", "water", "tunnel"});
  uint32_t const pedestrianType = classif().GetTypeByPath({"highway", "pedestrian", "area"});

  size_t waters = 0, pedestrians = 0;
  ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
  {
    if (fb.HasType(waterType))
      ++waters;
    if (fb.HasType(pedestrianType))
      ++pedestrians;
  });

  TEST_EQUAL(waters, 2, ());
  TEST_EQUAL(pedestrians, 4, ());
}

UNIT_CLASS_TEST(TestRawGenerator, Place_Region)
{
  uint32_t const regionType = classif().GetTypeByPath({"place", "region"});

  std::string const mwmName = "Region";
  std::string const worldMwmName = WORLD_FILE_NAME;
  BuildFB("./data/osm_test_data/place_region.osm", mwmName, true /* makeWorld */);

  size_t worldRegions = 0, countryRegions = 0;

  ForEachFB(worldMwmName, [&](feature::FeatureBuilder const & fb)
  {
    if (fb.HasType(regionType))
    {
      TEST(!fb.GetName().empty(), ());
      ++worldRegions;
    }
  });

  TEST_EQUAL(worldRegions, 1, ());
  worldRegions = 0;

  // Prepare features data source.
  for (auto const & name : { mwmName, worldMwmName })
  {
    BuildFeatures(name);
    BuildSearch(name);

    ForEachFeature(name, [&](std::unique_ptr<FeatureType> ft)
    {
      if (feature::TypesHolder(*ft).Has(regionType))
      {
        TEST_EQUAL(ft->GetGeomType(), feature::GeomType::Point, ());
        TEST(!ft->GetName(StringUtf8Multilang::kDefaultCode).empty(), ());

        if (name == worldMwmName)
          ++worldRegions;
        else
          ++countryRegions;
      }
    });
  }

  TEST_EQUAL(worldRegions, 1, ());
  TEST_EQUAL(countryRegions, 0, ());
}

UNIT_CLASS_TEST(TestRawGenerator, MiniRoundabout)
{
  uint32_t const roadType = classif().GetTypeByPath({"highway", "secondary"});

  std::string const mwmName = "MiniRoundabout";
  BuildFB("./data/osm_test_data/mini_roundabout.osm", mwmName, false /* makeWorld */);

  size_t roadsCount = 0;
  ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
  {
    if (fb.HasType(roadType))
      ++roadsCount;
  });

  // Splitted on 3 parts + 4 created roundabouts.
  TEST_EQUAL(roadsCount, 4 + 3, ());

  // Prepare features data source.
  BuildFeatures(mwmName);
  BuildRouting(mwmName, "United Kingdom");

  FrozenDataSource dataSource;
  auto const res = dataSource.RegisterMap(platform::LocalCountryFile::MakeTemporary(GetMwmPath(mwmName)));
  TEST_EQUAL(res.second, MwmSet::RegResult::Success, ());

  std::vector<uint32_t> roads, rounds;

  FeaturesLoaderGuard guard(dataSource, res.first);

  size_t const numFeatures = guard.GetNumFeatures();
  for (size_t id = 0; id < numFeatures; ++id)
  {
    auto ft = guard.GetFeatureByIndex(id);
    if (feature::TypesHolder(*ft).Has(roadType))
    {
      TEST_EQUAL(ft->GetGeomType(), feature::GeomType::Line, ());

      ft->ParseGeometry(FeatureType::BEST_GEOMETRY);
      size_t const ptsCount = ft->GetPointsCount();
      TEST_GREATER(ptsCount, 1, ());
      auto const firstPt = ft->GetPoint(0);
      auto const lastPt = ft->GetPoint(ptsCount - 1);
      LOG(LINFO, ("==", id, firstPt, lastPt));

      if ((lastPt.x - firstPt.x) > 0.2)
        roads.push_back(id);
      if (fabs(lastPt.x - firstPt.x) < 0.1)
        rounds.push_back(id);
    }
  }

  TEST_EQUAL(roads, std::vector<uint32_t>({1, 3, 5}), ());
  TEST_EQUAL(rounds, std::vector<uint32_t>({0, 2, 4, 6}), ());

  using namespace routing;

  RoadAccess access;
  ReadRoadAccessFromMwm(*(guard.GetHandle().GetValue()), VehicleType::Car, access);
  LOG(LINFO, (access));

  SpeedCamerasMapT camerasMap;
  ReadSpeedCamsFromMwm(*(guard.GetHandle().GetValue()), camerasMap);
  LOG(LINFO, (camerasMap));
}

namespace
{
std::string_view GetPostcode(FeatureType & ft)
{
  return ft.GetMetadata(feature::Metadata::FMD_POSTCODE);
}
} // namespace

UNIT_CLASS_TEST(TestRawGenerator, Postcode_Relations)
{
  std::string const mwmName = "Postcodes";
  BuildFB("./data/osm_test_data/postcode_relations.osm", mwmName, false /* makeWorld */);
  BuildFeatures(mwmName);

  size_t count = 0;
  ForEachFeature(mwmName, [&count](std::unique_ptr<FeatureType> ft)
  {
    auto const name = ft->GetName(StringUtf8Multilang::kDefaultCode);
    if (name == "Boulevard Malesherbes")
    {
      TEST_EQUAL(GetPostcode(*ft), "75017", ());
      ++count;
    }
    else if (name == "Facebook France")
    {
      TEST_EQUAL(GetPostcode(*ft), "75002", ());
      ++count;
    }
  });

  TEST_EQUAL(count, 2, ());
}

UNIT_CLASS_TEST(TestRawGenerator, Relation_Wiki)
{
  std::string const mwmName = "Relation";

  BuildFB("./data/osm_test_data/village_relation.osm", mwmName);

  uint32_t const villageType = classif().GetTypeByPath({"place", "village"});

  size_t count = 0;
  ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
  {
    switch (fb.GetGeomType())
    {
    case feature::GeomType::Point:
    {
      TEST(fb.HasType(villageType), ());
      ++count;
      TEST_EQUAL(fb.GetMetadata().Get(feature::Metadata::FMD_WIKIPEDIA),
                 "fr:Charmois-l'Orgueilleux", ());
      break;
    }
    case feature::GeomType::Line:
    {
      TEST(fb.GetMetadata().Get(feature::Metadata::FMD_WIKIPEDIA).empty(), ());
      break;
    }
    }
  });

  TEST_EQUAL(count, 1, ());
}

UNIT_CLASS_TEST(TestRawGenerator, AssociatedStreet_Wiki)
{
  uint32_t const roadType = classif().GetTypeByPath({"highway", "residential"});

  std::string const mwmName = "Street";
  BuildFB("./data/osm_test_data/associated_street.osm", mwmName, false /* makeWorld */);

  size_t count = 0;
  ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
  {
    if (fb.HasType(roadType))
    {
      TEST_EQUAL(fb.GetMetadata().Get(feature::Metadata::FMD_WIKIPEDIA), "uk:Вулиця Боричів Тік", ());
      ++count;
    }
  });

  TEST_EQUAL(count, 5, ());

  BuildFeatures(mwmName);
  generator::WikidataHelper wikidata(GetMwmPath(mwmName), GetGenInfo().GetIntermediateFileName(kWikidataFilename));

  count = 0;
  ForEachFeature(mwmName, [&](std::unique_ptr<FeatureType> ft)
  {
    if (feature::TypesHolder(*ft).Has(roadType))
    {
      ++count;
      auto const data = wikidata.GetWikidataId(ft->GetID().m_index);
      TEST(data, ());
      TEST_EQUAL(*data, "Q4471511", ());
    }
  });

  TEST_EQUAL(count, 5, ());
}

UNIT_TEST(Place_CityRelations)
{
  std::string const mwmName = "Cities";
  std::string const worldMwmName = WORLD_FILE_NAME;

  std::string const arrFiles[] = {
    // 1 Relation with many polygons + 1 Node.
    "./data/osm_test_data/gorlovka_city.osm",
    // 2 Relations + 1 Node
    "./data/osm_test_data/tver_city.osm",
    // 1 Relation + 1 Node with _different_ names.
    "./data/osm_test_data/reykjavik_city.osm",
    "./data/osm_test_data/berlin_city.osm",
    // Relation boundary is place=suburb, but border_type=city
    "./data/osm_test_data/riviera_beach_city.osm",
    "./data/osm_test_data/hotchkiss_town.osm",
    "./data/osm_test_data/voronezh_city.osm",
    "./data/osm_test_data/minsk_city.osm",

    // 1 boundary-only Relation + 1 Node
    "./data/osm_test_data/kadikoy_town.osm",
    // 2 Relations + 1 Node
    "./data/osm_test_data/stolbtcy_town.osm",
    // 1 Way + 1 Relation + 1 Node
    "./data/osm_test_data/dmitrov_town.osm",
    "./data/osm_test_data/lesnoy_town.osm",

    "./data/osm_test_data/pushkino_city.osm",
    "./data/osm_test_data/korday_town.osm",
    "./data/osm_test_data/bad_neustadt_town.osm",

    /// @todo We don't store villages in World now, but for the future!
    // 1 Relation + 1 Node (not linked with each other)
    //"./data/osm_test_data/palm_beach_village.osm",
  };

  ms::LatLon arrNotInBoundary[] = {
    {48.2071448, 37.9729054},   // gorlovka
    {56.9118261, 36.2258988},   // tver
    {64.0469397, -21.9772409},  // reykjavik
    {52.4013879, 13.0601531},   // berlin
    {26.7481191, -80.0836532},  // riviera beach
    {38.7981690, -107.7347750}, // hotchkiss
    {51.7505379, 39.5894547},   // voronezh
    {53.9170050, 27.8576710},   // minsk

    {41.0150982, 29.0213844},   // kadikoy
    {53.5086454, 26.6979711},   // stolbtcy
    {56.3752679, 37.3288391},   // dmitrov
    {54.0026933, 27.6356912},   // lesnoy

    {56.0807652, 37.9277319},   // pushkino
    {43.2347760, 74.7573240},   // korday
    {50.4006992, 10.2020744},   // bad_neustadt

    //{26.6757006, -80.0547346},  // palm beach
  };

  size_t constexpr kManyBoundriesUpperIndex = 8;

  static_assert(std::size(arrFiles) == std::size(arrNotInBoundary));

  for (size_t i = 0; i < std::size(arrFiles); ++i)
  {
    TestRawGenerator generator;
    generator.BuildFB(arrFiles[i], mwmName, true /* makeWorld */);

    auto const & checker = ftypes::IsCityTownOrVillageChecker::Instance();

    // Check that we have only 1 city without duplicates.
    size_t count = 0;
    generator.ForEachFB(worldMwmName, [&](feature::FeatureBuilder const & fb)
    {
      if (fb.GetGeomType() == feature::GeomType::Point)
      {
        ++count;
        TEST(checker(fb.GetTypes()), ());
        TEST(fb.GetRank() > 0, ());
      }
    });

    TEST_EQUAL(count, 1, ());

    count = 0;
    generator.ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
    {
      if (checker(fb.GetTypes()))
      {
        ++count;
        TEST(fb.GetRank() > 0, ());
      }
    });

    TEST_EQUAL(count, 1, ());

    // Build boundaries table.
    generator.BuildFeatures(worldMwmName);
    generator.BuildSearch(worldMwmName);

    // Check that we have valid boundary in World.
    FrozenDataSource dataSource;
    auto const res = dataSource.RegisterMap(platform::LocalCountryFile::MakeTemporary(generator.GetMwmPath(worldMwmName)));
    TEST_EQUAL(res.second, MwmSet::RegResult::Success, ());

    search::CitiesBoundariesTable table(dataSource);
    TEST(table.Load(), ());
    TEST_EQUAL(table.GetSize(), 1, ());

    FeaturesLoaderGuard guard(dataSource, res.first);
    bool foundCity = false;

    size_t const numFeatures = guard.GetNumFeatures();
    for (size_t id = 0; id < numFeatures; ++id)
    {
      auto ft = guard.GetFeatureByIndex(id);
      if (checker(*ft))
      {
        TEST_EQUAL(ft->GetGeomType(), feature::GeomType::Point, ());
        foundCity = true;

        search::CitiesBoundariesTable::Boundaries boundary;
        TEST(table.Get(ft->GetID(), boundary), ());
        TEST(boundary.HasPoint(ft->GetCenter()), ());
        TEST(!boundary.HasPoint(mercator::FromLatLon(arrNotInBoundary[i])), (i));

        if (i < kManyBoundriesUpperIndex)
          TEST_GREATER(boundary.GetCount(), 1, (i));
        else
          TEST_EQUAL(boundary.GetCount(), 1, (i));
      }
    }

    TEST(foundCity, ());
  }
}

UNIT_TEST(Place_CityRelations_IncludePoint)
{
  std::string const mwmName = "Cities";
  std::string const worldMwmName = WORLD_FILE_NAME;

  std::string const arrFiles[] = {
    "./data/osm_test_data/valentin_alsina_town.osm",
  };

  ms::LatLon arrInBoundary[] = {
    {-34.6699107, -58.4302163},   // valentin_alsina
  };

  for (size_t i = 0; i < std::size(arrFiles); ++i)
  {
    TestRawGenerator generator;
    generator.BuildFB(arrFiles[i], mwmName, true /* makeWorld */);

    auto const & checker = ftypes::IsCityTownOrVillageChecker::Instance();

    // Check that we have only 1 city without duplicates.
    size_t count = 0;
    generator.ForEachFB(worldMwmName, [&](feature::FeatureBuilder const & fb)
    {
      if (fb.GetGeomType() == feature::GeomType::Point)
      {
        ++count;
        TEST(checker(fb.GetTypes()), ());
        TEST(fb.GetRank() > 0, ());
      }
    });

    TEST_EQUAL(count, 1, ());

    // Build boundaries table.
    generator.BuildFeatures(worldMwmName);
    generator.BuildSearch(worldMwmName);

    // Check that we have valid boundary in World.
    FrozenDataSource dataSource;
    auto const res = dataSource.RegisterMap(platform::LocalCountryFile::MakeTemporary(generator.GetMwmPath(worldMwmName)));
    TEST_EQUAL(res.second, MwmSet::RegResult::Success, ());

    search::CitiesBoundariesTable table(dataSource);
    TEST(table.Load(), ());
    TEST_EQUAL(table.GetSize(), 1, ());

    FeaturesLoaderGuard guard(dataSource, res.first);
    bool foundCity = false;

    size_t const numFeatures = guard.GetNumFeatures();
    for (size_t id = 0; id < numFeatures; ++id)
    {
      auto ft = guard.GetFeatureByIndex(id);
      if (checker(*ft))
      {
        TEST_EQUAL(ft->GetGeomType(), feature::GeomType::Point, ());
        foundCity = true;

        search::CitiesBoundariesTable::Boundaries boundary;
        TEST(table.Get(ft->GetID(), boundary), ());
        TEST(boundary.HasPoint(ft->GetCenter()), ());
        TEST(boundary.HasPoint(mercator::FromLatLon(arrInBoundary[i])), (i));
      }
    }

    TEST(foundCity, ());
  }
}

UNIT_CLASS_TEST(TestRawGenerator, Place_NoCityBoundaries)
{
  std::string const mwmName = "Cities";
  std::string const worldMwmName = WORLD_FILE_NAME;

  BuildFB("./data/osm_test_data/no_boundary_towns.osm", mwmName, true /* makeWorld */);

  auto const & checker = ftypes::IsCityTownOrVillageChecker::Instance();

  // Check that we have only 2 cities without duplicates (Pargas, Қордай).
  size_t count = 0;
  ForEachFB(worldMwmName, [&](feature::FeatureBuilder const & fb)
  {
    if (fb.GetGeomType() == feature::GeomType::Point)
    {
      ++count;
      TEST(checker(fb.GetTypes()), ());
    }
  });

  TEST_EQUAL(count, 2, ());

  // Build boundaries table.
  BuildFeatures(worldMwmName);
  BuildSearch(worldMwmName);

  // Check that we have NO boundaries in World. They are removed because of "very big".
  FrozenDataSource dataSource;
  auto const res = dataSource.RegisterMap(platform::LocalCountryFile::MakeTemporary(GetMwmPath(worldMwmName)));
  TEST_EQUAL(res.second, MwmSet::RegResult::Success, ());

  search::CitiesBoundariesTable table(dataSource);
  TEST(table.Load(), ());
  TEST_EQUAL(table.GetSize(), 0, ());
}

UNIT_CLASS_TEST(TestRawGenerator, Place_2Villages)
{
  std::string const mwmName = "Villages";

  BuildFB("./data/osm_test_data/tarachevo_villages.osm", mwmName, false /* makeWorld */);

  auto const & checker = ftypes::IsCityTownOrVillageChecker::Instance();

  // Check that we have 2 villages (Тарачево).
  size_t count = 0;
  ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
  {
    if (fb.GetGeomType() == feature::GeomType::Point)
    {
      ++count;
      TEST(checker(fb.GetTypes()), ());
      TEST_EQUAL(fb.GetName(), "Тарачево", ());
    }
  });

  TEST_EQUAL(count, 2, ());
}

UNIT_CLASS_TEST(TestRawGenerator, Relation_Fence)
{
  std::string const mwmName = "Fences";

  BuildFB("./data/osm_test_data/fence_relation.osm", mwmName);

  uint32_t const fenceType = classif().GetTypeByPath({"barrier", "fence"});

  size_t count = 0;
  ForEachFB(mwmName, [&](feature::FeatureBuilder const & fb)
  {
    if (fb.GetGeomType() == feature::GeomType::Line)
    {
      ++count;
      TEST(fb.HasType(fenceType), ());
    }
  });
  TEST_EQUAL(count, 2, ());
}

} // namespace raw_generator_tests
