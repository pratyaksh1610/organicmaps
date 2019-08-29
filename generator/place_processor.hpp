#pragma once

#include "generator/cities_boundaries_builder.hpp"
#include "generator/feature_builder.hpp"

#include "geometry/mercator.hpp"
#include "geometry/tree4d.hpp"

#include "base/geo_object_id.hpp"

#include <functional>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace generator
{
template <typename T, template<typename, typename> class Container, typename Alloc = std::allocator<T>>
class ClustersFinder
{
public:
  using DistanceFunc = std::function<double(T const &)>;
  using IsSameFunc = std::function<bool(T const &, T const &)>;
  using ConstIterator = T const *;

  ClustersFinder(Container<T, Alloc> && container, DistanceFunc distanceFunc, IsSameFunc isSameFunc)
    : m_container(std::move(container)), m_distanseFunc(distanceFunc), m_isSameFunc(isSameFunc)
  {
    for (auto const & e : m_container)
      m_tree.Add(&e);
  }

  std::vector<std::vector<T>> Find()
  {
    std::vector<std::vector<T>> clusters;
    std::set<ConstIterator> unviewed;
    for (auto const & e : m_container)
      unviewed.insert(&e);

    while (!unviewed.empty())
      clusters.emplace_back(FindOneCluster(*std::cbegin(unviewed), unviewed));

    return clusters;
  }

private:
  struct TraitsDef
  {
    m2::RectD const LimitRect(ConstIterator const & it) const { return it->GetLimitRect(); }
  };

  std::vector<T> FindOneCluster(ConstIterator const & it, std::set<ConstIterator> & unviewed)
  {
    std::vector<T> cluster{*it};
    std::queue<ConstIterator> queue;
    queue.emplace(it);
    unviewed.erase(it);
    while (!queue.empty())
    {
      auto const current = queue.front();
      queue.pop();
      auto const queryBbox = GetBboxFor(current);
      m_tree.ForEachInRect(queryBbox, [&](auto const & conditate) {
        if (current == conditate || unviewed.count(conditate) == 0 || !m_isSameFunc(*current, *conditate))
          return;

        unviewed.erase(conditate);
        queue.emplace(conditate);
        cluster.emplace_back(*conditate);
      });
    }

    return cluster;
  }

  m2::RectD GetBboxFor(ConstIterator const & it)
  {
    m2::RectD bbox;
    it->GetLimitRect().ForEachCorner([&](auto const & p) {
      auto const dist = m_distanseFunc(*it);
      bbox.Add(MercatorBounds::RectByCenterXYAndSizeInMeters(p, dist));
    });

    return bbox;
  }

  Container<T, Alloc> m_container;
  DistanceFunc m_distanseFunc;
  IsSameFunc m_isSameFunc;
  m4::Tree<ConstIterator, TraitsDef> m_tree;
};

bool NeedProcessPlace(feature::FeatureBuilder const & fb);

// This structure encapsulates work with elements of different types.
// This means that we can consider a set of polygons of one relation as a single entity.
class FeaturePlace
{
public:
  using FeaturesBuilders = std::vector<feature::FeatureBuilder>;

  void Append(feature::FeatureBuilder const & fb);
  feature::FeatureBuilder const & GetFb() const;
  FeaturesBuilders const & GetFbs() const;
  m2::RectD const & GetLimitRect() const;
  base::GeoObjectId GetMostGenericOsmId() const;
  uint8_t GetRank() const;
  std::string GetName() const;
  m2::PointD GetKeyPoint() const;
  StringUtf8Multilang const & GetMultilangName() const;
  bool IsPoint() const;

private:
  m2::RectD m_limitRect;
  FeaturesBuilders m_fbs;
  size_t m_bestIndex;
};

// The class PlaceProcessor is responsible for the union of boundaries of the places.
class PlaceProcessor
{
public:
  using PlaceWithIds = std::pair<feature::FeatureBuilder, std::vector<base::GeoObjectId>>;

  PlaceProcessor(std::shared_ptr<OsmIdToBoundariesTable> boundariesTable = {});

  void Add(feature::FeatureBuilder const & fb);
  std::vector<PlaceWithIds> ProcessPlaces();

private:
  using FeaturePlaces = std::vector<FeaturePlace>;

  static std::string GetKey(feature::FeatureBuilder const & fb);
  void FillTable(FeaturePlaces::const_iterator start, FeaturePlaces::const_iterator end,
                 FeaturePlaces::const_iterator best) const;

  std::unordered_map<std::string, std::unordered_map<base::GeoObjectId, FeaturePlace>> m_nameToPlaces;
  std::shared_ptr<OsmIdToBoundariesTable> m_boundariesTable;
};
} // namespace generator
