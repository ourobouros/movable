/*******************************************************************************
 ** MOVABLE project - REDS Institute, HEIG-VD, Yverdon-les-Bains (CH) - 2016  **
 **                                                                           **
 ** This file is part of MOVABLE.                                             **
 **                                                                           **
 **  MOVABLE is free software: you can redistribute it and/or modify          **
 **  it under the terms of the GNU General Public License as published by     **
 **  the Free Software Foundation, either version 3 of the License, or        **
 **  (at your option) any later version.                                      **
 **                                                                           **
 **  MOVABLE is distributed in the hope that it will be useful,               **
 **  but WITHOUT ANY WARRANTY; without even the implied warranty of           **
 **  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the            **
 **  GNU General Public License for more details.                             **
 **                                                                           **
 **  You should have received a copy of the GNU General Public License        **
 **  along with MOVABLE.  If not, see <http://www.gnu.org/licenses/>.         **
 ******************************************************************************/

#include <iostream>
#include <vector>

#include <ctime>
#include <chrono>

#include "KernelBoost.hpp"

#ifdef MOVABLE_TRAIN
/* Constructor used in the training phase */
KernelBoost::KernelBoost(Parameters &params,
                         const SmoothingMatrices &SM,
                         const Dataset &dataset)
{
    boostedClassifiers.resize(dataset.getGtPairsNo());

    log_info("Creating a Boosted Classifier for each of the %d "
             "ground-truth pairs...", dataset.getGtPairsNo());
    for (unsigned int i = 0; i < dataset.getGtPairsNo(); ++i) {
        boostedClassifiers[i] = new BoostedClassifier(params, SM,
                                                      dataset, i);
    }

    /* Sanity check: cannot avoid AutoContext if more than 1 GT pair */
    assert (params.useAutoContext || dataset.getGtPairsNo() == 1);

    if (params.useAutoContext) {
        log_info("AutoContext requested, performing the final training step");

        /*
         * Classify the images in the dataset, convert the overall
         * classification problem in a binary one, and finally solve it using
         * the performed classifications as additional channels
         */
        params.posSamplesNo = params.finalSamplesNo;
        params.negSamplesNo = params.finalSamplesNo;
        params.treeDepth = params.finalTreeDepth;

        Dataset dataset_final(params, dataset, boostedClassifiers);
        finalClassifier = new BoostedClassifier(params, SM, dataset_final, 0);
    } else {
        /*
         * If a single pair of GT values is present and we use no AutoContext,
         * the final classifier is the one we have learned on the only pair we
         * have
         */
        finalClassifier = boostedClassifiers[0];
    }

    /*
     * Classify the training images to give a visual feedback and compute
     * statistics -- skipped for performance purposes
     */
#if 0
    std::vector< cv::Mat > scoreImages(dataset_final.getImagesNo());
    /* Perform the final classification on training images */
#pragma omp parallel for schedule(dynamic)
    for (unsigned int i = 0; i < dataset_final.getImagesNo(); ++i) {
        std::chrono::time_point< std::chrono::system_clock > start;
        std::chrono::time_point< std::chrono::system_clock > end;
        start = std::chrono::system_clock::now();

        EMat result;
        if (params.fastClassifier) {
            const sampleSet& ePoints = dataset_final.getEPoints(i);
            finalClassifier->classifyImage(dataset_final,
                                           i,
                                           ePoints,
                                           result);
        } else {
            /*
             * Prepare a vector containing the set of OpenCV
             * matrices corresponding to the available channels
             */
            std::vector< cv::Mat > chs;
            dataset_final.getChsForImage(i, chs);
            finalClassifier->classifyFullImage(chs,
                                               dataset_final.getBorderSize(),
                                               result);
        }
        normalizeImage(result,
                       dataset_final.getBorderSize(),
                       scoreImages[i]);
#ifndef TESTS
        saveClassifiedImage(result,
                            params.finalResDir,
                            dataset_final.getImageName(i),
                            dataset_final.getBorderSize());
        float MR = computeMR(result, dataset_final.getGt(0, i),
                             dataset_final.getBorderSize());
        end = std::chrono::system_clock::now();
        std::chrono::duration< double > elapsed_s = end-start;
        log_info("\t\tImage %d/%d DONE! (took %.3fs, MR=%.3f)",
                 i+1, dataset_final.getImagesNo(),
                 elapsed_s.count(), MR);
#endif // !TESTS
    }

    binaryThreshold = params.threshold;

#ifndef TESTS
    /* Dumping the thresholded images for reference */
    for (unsigned int i = 0; i < dataset_final.getImagesNo(); ++i) {
        saveThresholdedImage(scoreImages[i],
                             dataset_final.getMask(i),
                             binaryThreshold,
                             params.finalResDir,
                             dataset_final.getImageName(i),
                             dataset_final.getOriginalImgSize(i),
                             dataset_final.getBorderSize());
    }

    /* Save WL distribution statistics */
    std::string statsFName = params.baseResDir + "/statistics.txt";

    std::cerr << "SAVING STATS TO : " << statsFName << "\n";

    FILE *fp = fopen(statsFName.c_str(), "wt");
    if (fp != NULL) {
        for (unsigned int i = 0; i < boostedClassifiers.size(); ++i) {
            fprintf(fp, "________ BC %d (%d-%d) ________\n",
                    i,
                    dataset.getGtNegativePairValue(i),
                    dataset.getGtPositivePairValue(i));

            std::vector< int > count(dataset.getDataChNo(), 0);
            boostedClassifiers[i]->getChCount(count);
            float sum = 0;
            for (unsigned int j = 0; j < count.size(); ++j) {
                sum += count[j];
            }
            for (unsigned int j = 0; j < count.size(); ++j) {
                fprintf(fp, "ch %d/%d (%s): \t%d (%.2f%%)\n",
                        j+1, (int)count.size(),
                        params.channelList[j].c_str(),
                        count[j], (float)count[j]/sum*100);
            }
            fprintf(fp, "\n");
        }

        fprintf(fp, "________ FINAL CLASSIFIER ________\n");
        std::vector< int > count(dataset.getDataChNo()+
                                 boostedClassifiers.size(), 0);
        finalClassifier->getChCount(count);
        float sum = 0;
        for (unsigned int j = 0; j < count.size(); ++j) {
            sum += count[j];
        }
        for (unsigned int j = 0; j < count.size(); ++j) {
            if (j < params.channelList.size()) {
                fprintf(fp, "ch %d/%d (%s): \t%d (%.2f%%)\n",
                        j+1, (int)count.size(),
                        params.channelList[j].c_str(),
                        count[j], (float)count[j]/sum*100);
            } else {
                fprintf(fp, "ch %d/%d (class %d-%d): \t%d (%.2f%%)\n",
                        j+1, (int)count.size(),
                        dataset.getGtNegativePairValue(j-params.channelList.size()),
                        dataset.getGtPositivePairValue(j-params.channelList.size()),
                        count[j], (float)count[j]/sum*100);
            }
        }
        fprintf(fp, "\n");

        fclose (fp);
    } else {
        log_err("Cannot open statistics file %s for writing",
                statsFName.c_str());
    }
#endif // !TESTS
#endif // 0
}

KernelBoost::KernelBoost(std::string &descr_json)
{
    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(descr_json, root)) {
        throw std::runtime_error("invalidJSONDescription");
    }

    Deserialize(root);
}

#else // !MOVABLE_TRAIN

/* Constructor used in the testing phase */
KernelBoost::KernelBoost(std::string &descr_json,
                         const Parameters &params,
                         Dataset &dataset)
{
    Json::Value root;
    Json::Reader reader;

    if (!reader.parse(descr_json, root)) {
        throw std::runtime_error("invalidJSONDescription");
    }

    /*
     * This will point to the actual dataset we want to use (discriminating
     * between the case w/out AutoContext
     */
    Dataset *data_to_use;

    Deserialize(root);

    if (params.useAutoContext) {
        log_info("Start by performing individual pairs classification...");
        Dataset *dataset_final = new Dataset(dataset, boostedClassifiers);

        /* Perform the final classification on training images */
        log_info("Last round of classification on grouped GT values...");
        data_to_use = dataset_final;
    } else {
        /* Classifying the images in a single shot (no AutoContext) */
        data_to_use = &dataset;
    }

#ifndef TESTS
    std::chrono::time_point< std::chrono::system_clock > start;
    std::chrono::time_point< std::chrono::system_clock > end;
    start = std::chrono::system_clock::now();
#endif // !TESTS

// #pragma omp parallel for schedule(dynamic)
    for (unsigned int i = 0; i < data_to_use->getImagesNo(); ++i) {
        EMat result;
        if (params.fastClassifier) {
            const sampleSet& ePoints = data_to_use->getEPoints(i);
            finalClassifier->classifyImage(*data_to_use,
                                           i,
                                           ePoints,
                                           result);
        } else {
            /*
             * Prepare a vector containing the set of OpenCV matrices
             * corresponding to the available channels
             */
            std::vector< cv::Mat > chs;
            data_to_use->getChsForImage(i, chs);
            finalClassifier->classifyFullImage(chs,
                                               data_to_use->getBorderSize(),
                                               result);
        }
#ifndef TESTS
        saveClassifiedImage(result,
                            params.baseResDir,
                            data_to_use->getImageName(i),
                            data_to_use->getBorderSize());

        cv::Mat imgToThreshold(result.rows(), result.cols(), CV_32FC1);
        // cv::Mat imgToThreshold(result.rows(), result.cols(), CV_8UC1);
        cv::eigen2cv(result, imgToThreshold);
        saveThresholdedImage(imgToThreshold,
                             data_to_use->getMask(i),
                             params.threshold,
                             params.baseResDir,
                             data_to_use->getImageName(i),
                             data_to_use->getOriginalImgSize(i),
                             data_to_use->getBorderSize());

        saveOverlayedImage(data_to_use->getImagePath(i),
                           params.baseResDir,
                           data_to_use->getImageName(i));

        /* Removed for initial H3PoC tests */
#if 0
        cv::Mat normImage;

        /*
         * Here image normalization has been replaced by border cropping
         * followed by thresholding with a fixed threshold specified in
         * the configuration file
         */
        // normalizeImage(result, dataset_final.getBorderSize(), normImage);
        cv::Mat beforeCrop(result.rows(), result.cols(), CV_32FC1);
        cv::eigen2cv(result, beforeCrop);

        /* Drop border */
        normImage = beforeCrop(cv::Range(data_to_use->getBorderSize(),
                                         beforeCrop.rows-data_to_use->getBorderSize()),
                               cv::Range(data_to_use->getBorderSize(),
                                         beforeCrop.cols-data_to_use->getBorderSize()));

        saveThresholdedImage(normImage,
                             data_to_use->getMask(i),
                             params.threshold,
                             params.baseResDir,
                             data_to_use->getImageName(i),
                             data_to_use->getOriginalImgSize(i),
                             data_to_use->getBorderSize());

        saveOverlayedImage(data_to_use->getImagePath(i),
                           params.baseResDir,
                           data_to_use->getImageName(i));
#endif // 0
        log_info("\t\tImage %d/%d DONE!",
                 i+1, data_to_use->getImagesNo());
#endif // !TESTS
    }
#ifndef TESTS
    end = std::chrono::system_clock::now();
    std::chrono::duration< double > elapsed_s = end-start;
    log_info("CLASSIFICATION DONE! TOOK %.3fs", elapsed_s.count());
#endif // !TESTS
}
#endif // MOVABLE_TRAIN


KernelBoost::KernelBoost(Json::Value &root)
{
    Deserialize(root);
}

KernelBoost::~KernelBoost()
{
    for (unsigned int i = 0; i < boostedClassifiers.size(); ++i) {
        delete boostedClassifiers[i];
        boostedClassifiers[i] = nullptr;
    }
    /* Take care of the case where no AutoContext */
    if (boostedClassifiers[0] != nullptr) {
        delete finalClassifier;
    }
}

KernelBoost::KernelBoost(const KernelBoost &obj)
{
    /* Deallocate previous boosted classifiers */
    for (unsigned int i = 0; i < boostedClassifiers.size(); ++i) {
        delete boostedClassifiers[i];
        boostedClassifiers[i] = nullptr;
    }
    if (finalClassifier != nullptr) {
        /*
         * When not using AutoContext, finalClassifier is going to be equivalent
         * to boostedClassifiers[0], therefore we have to avoid a double free!
         */
        delete finalClassifier;
    }
    /* Resize the boosted classifiers vector */
    this->boostedClassifiers.resize(obj.boostedClassifiers.size());
    /*
     * Create the new set of boosted classifiers from the elements
     * of the copied one
     */
    for (unsigned int i = 0; i < boostedClassifiers.size(); ++i) {
        boostedClassifiers[i] =
            new BoostedClassifier(*(obj.boostedClassifiers[i]));
    }
    /* Handle the case of no AutoContext */
    if (obj.finalClassifier != obj.boostedClassifiers[0]) {
        finalClassifier = new BoostedClassifier(*(obj.finalClassifier));
    } else {
        finalClassifier = boostedClassifiers[0];
    }
    binaryThreshold = obj.binaryThreshold;
}

KernelBoost &
KernelBoost::operator=(const KernelBoost &rhs)
{
    if (this != &rhs) {
        /* Deallocate previous boosted classifiers */
        for (unsigned int i = 0; i < boostedClassifiers.size();
             ++i) {
            delete boostedClassifiers[i];
        }
        delete finalClassifier;

        /* Resize the weak learners vector */
        this->boostedClassifiers.resize(rhs.boostedClassifiers.size());
        /* Create the new set of weak learners from the elements
           of the copied one */
        for (unsigned int i = 0; i < boostedClassifiers.size(); ++i) {
            boostedClassifiers[i] =
                new BoostedClassifier(*(rhs.boostedClassifiers[i]));
        }
        /* Handle the case of no AutoContext */
        if (rhs.finalClassifier != rhs.boostedClassifiers[0]) {
            finalClassifier = new BoostedClassifier(*(rhs.finalClassifier));
        } else {
            finalClassifier = boostedClassifiers[0];
        }
        binaryThreshold = rhs.binaryThreshold;
    }

    return *this;
}

bool
operator==(const KernelBoost &kb1, const KernelBoost &kb2)
{
    if (kb1.boostedClassifiers.size() != kb2.boostedClassifiers.size())
        return false;

    for (unsigned int i = 0; i < kb1.boostedClassifiers.size(); ++i) {
        if (*(kb1.boostedClassifiers[i]) != *(kb2.boostedClassifiers[i]) ||
            *(kb1.finalClassifier) != *(kb2.finalClassifier) ||
            kb1.binaryThreshold != kb2.binaryThreshold)
            return false;
    }

    return true;
}

bool
operator!=(const KernelBoost &kb1, const KernelBoost &kb2)
{
    return !(kb1 == kb2);
}

void
KernelBoost::Serialize(Json::Value &)
{
    throw std::runtime_error("deprecatedSerialization");
}

void
KernelBoost::serialize(Json::Value &root, const Parameters &params)
{
    Json::Value kb_json(Json::objectValue);
    Json::Value boostedClassifiers_json(Json::arrayValue);
    for (unsigned int i = 0; i < boostedClassifiers.size(); ++i) {
        Json::Value bc_json(Json::objectValue);

        boostedClassifiers[i]->Serialize(bc_json);
        boostedClassifiers_json.append(bc_json);
    }
    kb_json["BoostedClassifiers"] = boostedClassifiers_json;

    if (params.useAutoContext) {
        Json::Value final_bc_json(Json::objectValue);
        finalClassifier->Serialize(final_bc_json);
        kb_json["FinalClassifier"] = final_bc_json;
    }
    kb_json["binaryThreshold"] = 0.0;
    kb_json["fastClassifier"] = params.fastClassifier;
    kb_json["RBCdetection"] = params.RBCdetection;
    kb_json["useAutoContext"] = params.useAutoContext;

    kb_json["sampleSize"] = params.sampleSize;
    kb_json["imgRescaleFactor"] = params.imgRescaleFactor;

    kb_json["houghMinDist"] = params.houghMinDist;
    kb_json["houghHThresh"] = params.houghHThresh;
    kb_json["houghLThresh"] = params.houghLThresh;
    kb_json["houghMinRad"] = params.houghMinRad;
    kb_json["houghMaxRad"] = params.houghMaxRad;

    for (unsigned int i = 0; i < params.channelList.size(); ++i) {
        kb_json["Channels"].append(params.channelList[i]);
    }

    root["KernelBoost"] = kb_json;
}

void
KernelBoost::Deserialize(Json::Value &root)
{
    for (Json::Value::iterator it =
             root["KernelBoost"]["BoostedClassifiers"].begin();
         it != root["KernelBoost"]["BoostedClassifiers"].end(); ++it) {
        BoostedClassifier *bc = new BoostedClassifier(*it);
        boostedClassifiers.push_back(bc);
    }

    if (root["KernelBoost"]["useAutoContext"].asBool()) {
        finalClassifier =
            new BoostedClassifier(root["KernelBoost"]["FinalClassifier"]);
    } else {
        finalClassifier = boostedClassifiers[0];
    }
    binaryThreshold =
        root["KernelBoost"]["binaryThreshold"].asDouble();
}
