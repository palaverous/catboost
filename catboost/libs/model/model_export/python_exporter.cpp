#include "python_exporter.h"

#include "export_helpers.h"

#include <library/resource/resource.h>

#include <util/generic/map.h>
#include <util/generic/set.h>
#include <util/string/builder.h>
#include <util/string/cast.h>
#include <util/stream/input.h>
#include <util/stream/file.h>

namespace NCatboost {
    using namespace NCatboostModelExportHelpers;

    /*
     * Full model code with complete support of cat features
     */

    void TCatboostModelToPythonConverter::WriteCTRStructs() {
        Out << NResource::Find("catboost_model_export_python_ctr_structs");
    };

    static void WriteModelCTRs(IOutputStream& out, const TFullModel& model, TIndent& indent) {
        const TVector<TModelCtr>& neededCtrs = model.ObliviousTrees.GetUsedModelCtrs();
        if (neededCtrs.empty()) {
            return;
        }

        TSequenceCommaSeparator comma;
        out << indent++ << "model_ctrs = catboost_model_ctrs_container(" << '\n';

        const TStaticCtrProvider* ctrProvider = dynamic_cast<TStaticCtrProvider*>(model.CtrProvider.Get());
        Y_VERIFY(ctrProvider, "Unsupported CTR provider");

        TVector<TCompressedModelCtr> compressedModelCtrs = CompressModelCtrs(neededCtrs);

        out << indent << "used_model_ctrs_count = " << model.ObliviousTrees.GetUsedModelCtrs().size() << "," << '\n';
        out << indent++ << "compressed_model_ctrs = [" << '\n';

        comma.ResetCount(compressedModelCtrs.size());
        for (const auto& compressedCtr : compressedModelCtrs) {
            TSequenceCommaSeparator commaInner;

            out << indent++ << "catboost_compressed_model_ctr(" << '\n';

            out << indent++ << "projection = catboost_projection(" << '\n';

            const TFeatureCombination& proj = *compressedCtr.Projection;
            TVector<int> transposedCatFeatureIndexes;
            out << indent << "transposed_cat_feature_indexes = [";
            TSequenceCommaSeparator commaInnerWithSpace(proj.CatFeatures.size(), AddSpaceAfterComma);
            for (const auto feature : proj.CatFeatures) {
                out << feature << commaInnerWithSpace;
            }
            out << "]," << '\n';
            out << indent++ << "binarized_indexes = [";
            commaInner.ResetCount(proj.BinFeatures.size() + proj.OneHotFeatures.size());
            for (const auto& feature : proj.BinFeatures) {
                const TBinFeatureIndexValue& featureValue = ctrProvider->GetFloatFeatureIndexes().at(feature);
                out << '\n' << indent << "catboost_bin_feature_index_value(";
                out << "bin_index = " << featureValue.BinIndex << ", ";
                out << "check_value_equal = " << featureValue.CheckValueEqual << ", ";
                out << "value = " << (int)featureValue.Value;
                out << ")" << commaInner;
            }
            for (const auto& feature : proj.OneHotFeatures) {
                const TBinFeatureIndexValue& featureValue = ctrProvider->GetOneHotFeatureIndexes().at(feature);
                out << '\n' << indent << "catboost_bin_feature_index_value(";
                out << "bin_index = " << featureValue.BinIndex << ", ";
                out << "check_value_equal = " << featureValue.CheckValueEqual << ", ";
                out << "value = " << (int)featureValue.Value;
                out << ")" << commaInner;
            }
            --indent;
            if (proj.BinFeatures.size() > 0 || proj.OneHotFeatures.size() > 0) {
                out << '\n' << indent;
            }
            out << "]" << '\n';

            out << --indent << ")," << '\n';
            out << indent++ << "model_ctrs = [" << '\n';
            commaInner.ResetCount(compressedCtr.ModelCtrs.size());
            for (const auto& ctr : compressedCtr.ModelCtrs) {
                TSequenceCommaSeparator commaLocal(7, AddSpaceAfterComma);
                out << indent << "catboost_model_ctr(";
                out << "base_hash = " << ctr->Base.GetHash() << commaLocal;
                out << "base_ctr_type = \"" << ctr->Base.CtrType << "\"" << commaLocal ;
                out << "target_border_idx = " << ctr->TargetBorderIdx << commaLocal;
                out << "prior_num = " << ctr->PriorNum << commaLocal;
                out << "prior_denom = " << ctr->PriorDenom << commaLocal;
                out << "shift = " << ctr->Shift << commaLocal;
                out << "scale = " << ctr->Scale << commaLocal;
                out << ")" << commaInner << '\n';
            }
            out << --indent << "]" << '\n';
            out << --indent << ")" << comma << '\n';
        }
        out << --indent << "]," << '\n';
        out << indent++ << "ctr_data = catboost_ctr_data(" << '\n';
        out << indent++ << "learn_ctrs = {" << '\n';
        comma.ResetCount(ctrProvider->CtrData.LearnCtrs.size());
        TMap<ui64, const TCtrValueTable*> orderedLearnCtrs;
        for (const auto& learnCtr : ctrProvider->CtrData.LearnCtrs) {
            orderedLearnCtrs.emplace(learnCtr.first.GetHash(), &learnCtr.second);
        }
        for (const auto& orderedLearnCtr : orderedLearnCtrs) {
            const auto& learnCtrValueTable = *orderedLearnCtr.second;
            TSequenceCommaSeparator commaInner(AddSpaceAfterComma);
            out << indent << orderedLearnCtr.first << " :" << '\n';
            out << indent++ << "catboost_ctr_value_table(" << '\n';
            out << indent << "index_hash_viewer = {";
            const TConstArrayRef<TBucket> HashViewerBuckets = learnCtrValueTable.GetIndexHashViewer().GetBuckets();
            commaInner.ResetCount(HashViewerBuckets.size());
            for (const auto& bucket : HashViewerBuckets) {
                out << bucket.Hash << " : " << bucket.IndexValue << commaInner;
            }
            out << "}," << '\n';
            out << indent << "target_classes_count = " << learnCtrValueTable.TargetClassesCount << "," << '\n';
            out << indent << "counter_denominator = " << learnCtrValueTable.CounterDenominator << "," << '\n';
            const TConstArrayRef<TCtrMeanHistory> ctrMeanHistories = learnCtrValueTable.GetTypedArrayRefForBlobData<TCtrMeanHistory>();
            out << indent << "ctr_mean_history = [";
            commaInner.ResetCount(ctrMeanHistories.size());
            for (const auto& ctrMean : ctrMeanHistories) {
                out << "catboost_ctr_mean_history(";
                out << "sum = " << ctrMean.Sum << ", ";
                out << "count = " << ctrMean.Count;
                out << ")" << commaInner;
            }
            out << "]," << '\n';
            const TConstArrayRef<int> ctrTotal = learnCtrValueTable.GetTypedArrayRefForBlobData<int>();
            out << indent << "ctr_total = [" << OutputArrayInitializer(ctrTotal) << "]" << '\n';
            out << --indent << ")" << comma << '\n';
        };
        out << --indent << "}" << '\n';
        out << --indent << ")" << '\n';
        out << --indent << ")" << '\n';
    };


    void TCatboostModelToPythonConverter::WriteModelCatFeatures(const TFullModel& model, const THashMap<int, TString>* catFeaturesHashToString) {
        CB_ENSURE(model.ObliviousTrees.ApproxDimension == 1, "Export of MultiClassification model to Python is not supported.");

        if (!model.ObliviousTrees.GetUsedModelCtrs().empty()) {
            WriteCTRStructs();
        }

        TIndent indent(0);
        TSequenceCommaSeparator comma;
        Out << indent << "###  Model data" << '\n';

        Out << indent++ << "class catboost_model(object):" << '\n';
        Out << indent << "float_features_index = [\n";
        TStringBuilder str;
        for (const auto& feature: model.ObliviousTrees.FloatFeatures) {
            str << feature.FeatureIndex << ", ";
        }
        str.pop_back();
        Out << ++indent << str << "\n";
        Out << --indent << "]\n";
        int max_index = -1;
        for (const auto& feature: model.ObliviousTrees.FloatFeatures) {
            max_index = Max(max_index, feature.FeatureIndex);
        }
        Out << indent << "float_feature_count = " << max_index + 1 << '\n';
        max_index = -1;
        for (const auto& feature: model.ObliviousTrees.CatFeatures) {
            max_index = Max(max_index, feature.FeatureIndex);
        }
        Out << indent << "cat_feature_count = " << max_index + 1 << '\n';
        Out << indent << "binary_feature_count = " << model.ObliviousTrees.GetEffectiveBinaryFeaturesBucketsCount() << '\n';
        Out << indent << "tree_count = " << model.ObliviousTrees.TreeSizes.size() << '\n';

        Out << indent++ << "float_feature_borders = [" << '\n';
        comma.ResetCount(model.ObliviousTrees.FloatFeatures.size());
        for (const auto& floatFeature : model.ObliviousTrees.FloatFeatures) {
            Out << indent << "["
                << OutputArrayInitializer([&floatFeature](size_t i) { return FloatToString(floatFeature.Borders[i], PREC_NDIGITS, 8); }, floatFeature.Borders.size())
                << "]" << comma << '\n';
        }
        Out << --indent << "]" << '\n';

        Out << indent << "tree_depth = [" << OutputArrayInitializer(model.ObliviousTrees.TreeSizes) << "]" << '\n';

        const TVector<TRepackedBin>& bins = model.ObliviousTrees.GetRepackedBins();
        Out << indent << "tree_split_border = [" << OutputArrayInitializer([&bins](size_t i) { return (int)bins[i].SplitIdx; }, bins.size()) << "]" << '\n';
        Out << indent << "tree_split_feature_index = [" << OutputArrayInitializer([&bins](size_t i) { return (int)bins[i].FeatureIndex; }, bins.size()) << "]" << '\n';
        Out << indent << "tree_split_xor_mask = [" << OutputArrayInitializer([&bins](size_t i) { return (int)bins[i].XorMask; }, bins.size()) << "]" << '\n';

        Out << indent << "cat_features_index = ["
            << OutputArrayInitializer([&model](size_t i) { return model.ObliviousTrees.CatFeatures[i].FeatureIndex; }, model.ObliviousTrees.CatFeatures.size()) << "]" << '\n';

        Out << indent << "one_hot_cat_feature_index = ["
            << OutputArrayInitializer([&model](size_t i) { return model.ObliviousTrees.OneHotFeatures[i].CatFeatureIndex; }, model.ObliviousTrees.OneHotFeatures.size())
            << "]" << '\n';

        Out << indent++ << "one_hot_hash_values = [" << '\n';
        comma.ResetCount(model.ObliviousTrees.OneHotFeatures.size());
        for (const auto& oneHotFeature : model.ObliviousTrees.OneHotFeatures) {
            Out << indent << "["
                << OutputArrayInitializer([&oneHotFeature](size_t i) { return oneHotFeature.Values[i]; }, oneHotFeature.Values.size())
                << "]" << comma << '\n';
        }
        Out << --indent << "]" << '\n';

        Out << indent++ << "ctr_feature_borders = [" << '\n';
        comma.ResetCount(model.ObliviousTrees.CtrFeatures.size());
        for (const auto& ctrFeature : model.ObliviousTrees.CtrFeatures) {
            Out << indent << "["
                << OutputArrayInitializer([&ctrFeature](size_t i) { return FloatToString(ctrFeature.Borders[i], PREC_NDIGITS, 16); }, ctrFeature.Borders.size())
                << "]" << comma << '\n';
        }
        Out << --indent << "]" << '\n';

        int leafValueCount = 0;
        for (const auto& treeSize : model.ObliviousTrees.TreeSizes) {
            leafValueCount += treeSize * model.ObliviousTrees.ApproxDimension;
        }
        Out << '\n';
        Out << indent << "## Aggregated array of leaf values for trees. Each tree is represented by a separate line:" << '\n';
        Out << indent << "leaf_values = [" << OutputLeafValues(model, indent) << indent << "]" << '\n';

        if (!model.ObliviousTrees.GetUsedModelCtrs().empty()) {
            WriteModelCTRs(Out, model, indent);
            Out << '\n' << '\n';
            Out << NResource::Find("catboost_model_export_python_ctr_calcer") << '\n';
        }
        indent--;
        Out << indent++ << "cat_features_hashes = {" << '\n';
        if (catFeaturesHashToString != nullptr) {
            TSet<int> ordered_keys;
            for (const auto& key_value: *catFeaturesHashToString) {
                ordered_keys.insert(key_value.first);
            }
            for (const auto& key_value: ordered_keys) {
                Out << indent << "\"" << catFeaturesHashToString->at(key_value) << "\": "  << key_value << ",\n";
            }
        }
        Out << --indent << "}" << '\n';
        Out << '\n';
    };

    void TCatboostModelToPythonConverter::WriteApplicatorCatFeatures() {
        Out << NResource::Find("catboost_model_export_python_model_applicator") << '\n';
    };

}
