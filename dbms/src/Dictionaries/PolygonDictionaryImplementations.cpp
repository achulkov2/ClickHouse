#include "PolygonDictionaryImplementations.h"
#include "DictionaryFactory.h"

#include <DataTypes/DataTypeArray.h>
#include <DataTypes/DataTypeTuple.h>
#include <DataTypes/DataTypesNumber.h>

#include <common/logger_useful.h>

#include <numeric>

namespace DB
{

SimplePolygonDictionary::SimplePolygonDictionary(
        const std::string & database_,
        const std::string & name_,
        const DictionaryStructure & dict_struct_,
        DictionarySourcePtr source_ptr_,
        const DictionaryLifetime dict_lifetime_,
        InputType input_type_,
        PointType point_type_):
        IPolygonDictionary(database_, name_, dict_struct_, std::move(source_ptr_), dict_lifetime_, input_type_, point_type_)
{
}

std::shared_ptr<const IExternalLoadable> SimplePolygonDictionary::clone() const
{
    return std::make_shared<SimplePolygonDictionary>(
            this->database,
            this->name,
            this->dict_struct,
            this->source_ptr->clone(),
            this->dict_lifetime,
            this->input_type,
            this->point_type);
}

bool SimplePolygonDictionary::find(const Point &point, size_t & id) const
{
    bool found = false;
    for (size_t i = 0; i < polygons.size(); ++i)
    {
        if (bg::covered_by(point, polygons[i]))
        {
            id = i;
            found = true;
            break;
        }
    }
    return found;
}

GridPolygonDictionary::GridPolygonDictionary(
        const std::string & database_,
        const std::string & name_,
        const DictionaryStructure & dict_struct_,
        DictionarySourcePtr source_ptr_,
        const DictionaryLifetime dict_lifetime_,
        InputType input_type_,
        PointType point_type_):
        IPolygonDictionary(database_, name_, dict_struct_, std::move(source_ptr_), dict_lifetime_, input_type_, point_type_),
        grid(kMinIntersections, kMaxDepth, polygons) {}

std::shared_ptr<const IExternalLoadable> GridPolygonDictionary::clone() const
{
    return std::make_shared<GridPolygonDictionary>(
            this->database,
            this->name,
            this->dict_struct,
            this->source_ptr->clone(),
            this->dict_lifetime,
            this->input_type,
            this->point_type);
}

bool GridPolygonDictionary::find(const Point &point, size_t & id) const
{
    bool found = false;
    auto cell = grid.find(point.get<0>(), point.get<1>());
    if (cell)
    {
        for (size_t i = 0; i < (cell->polygon_ids).size(); ++i)
        {
            const auto & candidate = (cell->polygon_ids)[i];
            if ((cell->is_covered_by)[i] || bg::covered_by(point, polygons[candidate]))
            {
                found = true;
                id = candidate;
                break;
            }
        }
    }
    return found;
}

SmartPolygonDictionary::SmartPolygonDictionary(
        const std::string & database_,
        const std::string & name_,
        const DictionaryStructure & dict_struct_,
        DictionarySourcePtr source_ptr_,
        const DictionaryLifetime dict_lifetime_,
        InputType input_type_,
        PointType point_type_)
        : IPolygonDictionary(database_, name_, dict_struct_, std::move(source_ptr_), dict_lifetime_, input_type_, point_type_),
          grid(kMinIntersections, kMaxDepth, polygons)
{
    auto log = &Logger::get("BucketsPolygonIndex");
    buckets.reserve(polygons.size());
    for (size_t i = 0; i < polygons.size(); ++i)
    {
        buckets.emplace_back(std::vector<Polygon>{polygons[i]});
        LOG_TRACE(log, "Finished polygon" << i);
    }
}

std::shared_ptr<const IExternalLoadable> SmartPolygonDictionary::clone() const
{
    return std::make_shared<SmartPolygonDictionary>(
            this->database,
            this->name,
            this->dict_struct,
            this->source_ptr->clone(),
            this->dict_lifetime,
            this->input_type,
            this->point_type);
}

bool SmartPolygonDictionary::find(const Point & point, size_t & id) const
{
    /*
    bool found = false;
    double area = 0;
    for (size_t i = 0; i < polygons.size(); ++i)
    {
        size_t unused;
        if (buckets[i].find(point, unused))
        {
            double new_area = areas[i];
            if (!found || new_area < area)
            {
                found = true;
                id = i;
                area = new_area;
            }
        }
    }
    return found;
    */
    bool found = false;
    auto cell = grid.find(point.get<0>(), point.get<1>());
    if (cell)
    {
        for (size_t i = 0; i < (cell->polygon_ids).size(); ++i)
        {
            const auto & candidate = (cell->polygon_ids)[i];
            size_t unused = 0;
            if ((cell->is_covered_by)[i] || buckets[candidate].find(point, unused))
            {
                found = true;
                id = candidate;
                break;
            }
        }
    }
    return found;
}

OneBucketPolygonDictionary::OneBucketPolygonDictionary(
    const std::string & database_,
    const std::string & name_,
    const DictionaryStructure & dict_struct_,
    DictionarySourcePtr source_ptr_,
    const DictionaryLifetime dict_lifetime_,
    InputType input_type_,
    PointType point_type_)
    : IPolygonDictionary(database_, name_, dict_struct_, std::move(source_ptr_), dict_lifetime_, input_type_, point_type_),
      buckets_idxs()
{
    assert(this->polygons.size() > 0);

    size_t n = this->polygons.size();
    std::vector<Float64> polygon_min_y(n, std::numeric_limits<Float64>::max());
    std::vector<Float64> polygon_max_y(n, std::numeric_limits<Float64>::min());
    BucketsPolygonIndex all_idx(this->polygons);
    for (const auto & edge : all_idx.all_edges)
    {
        size_t id = edge.polygon_id;
        polygon_min_y[id] = std::min({polygon_min_y[id], edge.l.y(), edge.r.y()});
        polygon_max_y[id] = std::max({polygon_max_y[id], edge.l.y(), edge.r.y()});
    }
    this->min_y = *std::min_element(polygon_min_y.begin(), polygon_min_y.end());
    this->max_y = *std::max_element(polygon_max_y.begin(), polygon_max_y.end());
    this->step = (this->max_y - this->min_y) / kLinesCount;

    for (size_t i = 0; i < kLinesCount; ++i)
    {
        Float64 current_min = this->min_y + this->step * i;
        Float64 current_max = this->max_y - this->step * (kLinesCount - 1 - i);
        std::vector<Polygon> current;
        for (size_t j = 0; j < n; ++j)
        {
            if (std::max(current_min, polygon_min_y[j]) <= std::min(current_max, polygon_max_y[j]))
            {
                current.emplace_back(this->polygons[j]);
            }
        }
        this->buckets_idxs.emplace_back(current);
    }
}

std::shared_ptr<const IExternalLoadable> OneBucketPolygonDictionary::clone() const
{
    return std::make_shared<OneBucketPolygonDictionary>(
            this->database,
            this->name,
            this->dict_struct,
            this->source_ptr->clone(),
            this->dict_lifetime,
            this->input_type,
            this->point_type);
}

bool OneBucketPolygonDictionary::find(const Point & point, size_t & id) const
{
    if (point.y() < this->min_y || point.y() > this->max_y)
    {
        return false;
    }
    size_t pos = (point.y() - this->min_y) / this->step;
    if (pos >= this->buckets_idxs.size())
    {
        pos = this->buckets_idxs.size() - 1;
    }
    return this->buckets_idxs[pos].find(point, id);
}

template <class PolygonDictionary>
DictionaryPtr createLayout(const std::string &,
                           const DictionaryStructure & dict_struct,
                           const Poco::Util::AbstractConfiguration & config,
                           const std::string & config_prefix,
                           DictionarySourcePtr source_ptr)
{
    const String database = config.getString(config_prefix + ".database", "");
    const String name = config.getString(config_prefix + ".name");

    if (!dict_struct.key)
        throw Exception{"'key' is required for a polygon dictionary", ErrorCodes::BAD_ARGUMENTS};
    if (dict_struct.key->size() != 1)
        throw Exception{"The 'key' should consist of a single attribute for a polygon dictionary",
                        ErrorCodes::BAD_ARGUMENTS};
    IPolygonDictionary::InputType input_type;
    IPolygonDictionary::PointType point_type;
    const auto key_type = (*dict_struct.key)[0].type;
    const auto f64 = std::make_shared<DataTypeFloat64>();
    const auto multi_polygon_array = DataTypeArray(std::make_shared<DataTypeArray>(std::make_shared<DataTypeArray>(std::make_shared<DataTypeArray>(f64))));
    const auto multi_polygon_tuple = DataTypeArray(std::make_shared<DataTypeArray>(std::make_shared<DataTypeArray>(std::make_shared<DataTypeTuple>(std::vector<DataTypePtr>{f64, f64}))));
    const auto simple_polygon_array = DataTypeArray(std::make_shared<DataTypeArray>(f64));
    const auto simple_polygon_tuple = DataTypeArray(std::make_shared<DataTypeTuple>(std::vector<DataTypePtr>{f64, f64}));
    if (key_type->equals(multi_polygon_array))
    {
        input_type = IPolygonDictionary::InputType::MultiPolygon;
        point_type = IPolygonDictionary::PointType::Array;
    }
    else if (key_type->equals(multi_polygon_tuple))
    {
        input_type = IPolygonDictionary::InputType::MultiPolygon;
        point_type = IPolygonDictionary::PointType::Tuple;
    }
    else if (key_type->equals(simple_polygon_array))
    {
        input_type = IPolygonDictionary::InputType::SimplePolygon;
        point_type = IPolygonDictionary::PointType::Array;
    }
    else if (key_type->equals(simple_polygon_tuple))
    {
        input_type = IPolygonDictionary::InputType::SimplePolygon;
        point_type = IPolygonDictionary::PointType::Tuple;
    }
    else
        throw Exception{"The key type " + key_type->getName() +
                        " is not one of the following allowed types for a polygon dictionary: " +
                        multi_polygon_array.getName() + " " +
                        multi_polygon_tuple.getName() + " " +
                        simple_polygon_array.getName() + " " +
                        simple_polygon_tuple.getName() + " ",
                        ErrorCodes::BAD_ARGUMENTS};

    if (dict_struct.range_min || dict_struct.range_max)
        throw Exception{name
                        + ": elements range_min and range_max should be defined only "
                          "for a dictionary of layout 'range_hashed'",
                        ErrorCodes::BAD_ARGUMENTS};

    const DictionaryLifetime dict_lifetime{config, config_prefix + ".lifetime"};
    return std::make_unique<PolygonDictionary>(database, name, dict_struct, std::move(source_ptr), dict_lifetime, input_type, point_type);
};

void registerDictionaryPolygon(DictionaryFactory & factory)
{

    factory.registerLayout("polygon", createLayout<SimplePolygonDictionary>, true);
    factory.registerLayout("grid_polygon", createLayout<GridPolygonDictionary>, true);
    factory.registerLayout("bucket_polygon", createLayout<SmartPolygonDictionary>, true);
    factory.registerLayout("one_bucket_polygon", createLayout<OneBucketPolygonDictionary>, true);
}

}
