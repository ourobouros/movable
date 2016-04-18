/*******************************************************************************
** MOVABLE project - REDS Institute, HEIG-VD, Yverdon-les-Bains (CH) - 2016   **
**                                                                            **
** This file is part of MOVABLE.                                              **
**                                                                            **
**  MOVABLE is free software: you can redistribute it and/or modify           **
**  it under the terms of the GNU General Public License as published by      **
**  the Free Software Foundation, either version 3 of the License, or         **
**  (at your option) any later version.                                       **
**                                                                            **
**  MOVABLE is distributed in the hope that it will be useful,                **
**  but WITHOUT ANY WARRANTY; without even the implied warranty of            **
**  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the             **
**  GNU General Public License for more details.                              **
**                                                                            **
**  You should have received a copy of the GNU General Public License         **
**  along with MOVABLE.  If not, see <http://www.gnu.org/licenses/>.          **
*******************************************************************************/

#include <iostream>
#include <vector>
#include <cassert>

#include "WeakLearner.hpp"
#include "utils.hpp"

#ifdef MOVABLE_TRAIN

WeakLearner::WeakLearner(const Parameters &params,
			 const SmoothingMatrices &SM,
			 const Dataset &dataset,
			 const sampleSet &samplePositions,
			 EVec &weights,
			 const EVec &labels,
			 EVec &currentResponse)
{
#ifndef TESTS
	std::chrono::time_point< std::chrono::system_clock > start;
	std::chrono::time_point< std::chrono::system_clock > end;
	start = std::chrono::system_clock::now();
#endif /* TESTS */

	std::vector< FilterBank > filterBanks;
	std::vector< EMat > all_features(dataset.getDataChNo());

	/* Split the training samples in three parts (the first one to train the
	   filters, the second to train the tree, and the overall set (including
	   the unused third part) to compute the weight of the weak learner */
	const unsigned int subsetSize = floor(samplePositions.size() / 3);
	sampleSet samples_fl;
	sampleSet samples_tree;
	EVec Y_fl;
	EVec Y_tree;
	EVec W_fl;
	EVec W_tree;
	splitSampleSet(samplePositions, labels, weights, subsetSize,
		       samples_fl, samples_tree, Y_fl, Y_tree, W_fl, W_tree);

	/* For each channel, learn a filter bank and compute the features for
	   tree learning */
	for (unsigned int iC = 0; iC < dataset.getDataChNo(); ++iC) {
		log_info("\t\tLearning filters on channel %d/%d...",
			 (int)iC+1, (int)dataset.getDataChNo());
		/* Learn a new filter bank on the current channel, then add it
		   to the set of filter banks */
		FilterBank fltb(params, SM, dataset, iC, samples_fl, W_fl);
		filterBanks.push_back(fltb);
		/* Compute the features for the obtained filters */
		EMat feats;
		log_info("\t\tEvaluating filters on %d tree learning samples...",
			 (int)samples_tree.size());
		fltb.evaluateFilters(dataset, samples_tree, feats);
		all_features[iC] = feats;
	}
	/* Assemble the features in a single matrix */
	unsigned int featureCount = 0;
	for (unsigned int iC = 0; iC < dataset.getDataChNo(); ++iC) {
		featureCount += all_features[iC].cols();
	}
	EMat features(samples_tree.size(), featureCount);
	featureCount = 0;
	for (unsigned int iC = 0; iC < dataset.getDataChNo(); ++iC) {
		features.block(0,
			       featureCount,
			       samples_tree.size(),
			       all_features[iC].cols()) = all_features[iC];
		featureCount += all_features[iC].cols();
	}

	/* Train the tree on features from all the channels, receiving back the
	   list of features that have been retained */
	log_info("\t\tLearning a regression tree on %d features...",
		 (int)featureCount);
	std::vector< unsigned int > retainedFeatIdxs;
	rt = new RegTree(features, Y_tree, W_tree, params.treeDepth,
			 retainedFeatIdxs);
	all_features.clear();

	/* Build a new filter bank with the retained filters */
	fb = new FilterBank(filterBanks, retainedFeatIdxs);
	filterBanks.clear();

	/* Evaluate the filter bank on the samples ("features" can be reused
	   here) */
	log_info("\t\tEvaluating the %d selected features on the whole "
		 "training set...",  (int)retainedFeatIdxs.size());
	fb->evaluateFilters(dataset, samplePositions, features);

	/* Perform a tree prediction on the newly-computed features */
	EVec wl_response;
	rt->predict(features, wl_response);

	LineSearch ls(labels, currentResponse, wl_response);
	alpha = ls.run();
	log_info("\t\tComputed alpha value: %.6f", alpha);

	alpha *= params.shrinkageFactor;
	currentResponse += alpha * wl_response;

	weights = (-labels.cwiseProduct(currentResponse)).array().exp();

#ifndef TESTS
	MR = (labels.cwiseProduct(currentResponse).array() < 0).count()
		/ (float)samplePositions.size();

	loss = (-labels.cwiseProduct(currentResponse)).array().exp().sum()
		/ (float)samplePositions.size();

	end = std::chrono::system_clock::now();
	std::chrono::duration< double > elapsed_s = end-start;

	log_info("\tWeak learner trained, MR = %.3f, loss = %.3f, took %.3fs",
		MR, loss, elapsed_s.count());
#endif /* TESTS */
}
#endif /* MOVABLE_TRAIN */

WeakLearner::WeakLearner(std::string &descr_json)
{
	Json::Value root;
	Json::Reader reader;

	if (!reader.parse(descr_json, root)) {
		throw std::runtime_error("invalidJSONDescription");
	}

	Deserialize(root);
}

void
WeakLearner::Serialize(Json::Value &root)
{
	Json::Value wl_json(Json::objectValue);
	Json::Value fb_json(Json::objectValue);
	fb->Serialize(fb_json);
	wl_json["fb"] = fb_json;
	Json::Value rt_json(Json::objectValue);
	rt->Serialize(rt_json);
	wl_json["rt"] = rt_json;
	Json::Value params_json(Json::objectValue);
	params_json["alpha"] = alpha;
	params_json["MR"] = MR;
	params_json["loss"] = loss;
	wl_json["params"] = params_json;
	root["WeakLearner"] = wl_json;
}

void
WeakLearner::Deserialize(Json::Value &root)
{
	fb = new FilterBank(root["WeakLearner"]["fb"]);
	rt = new RegTree(root["WeakLearner"]["rt"]);
	alpha = root["WeakLearner"]["params"]["alpha"].asDouble();
	MR = root["WeakLearner"]["params"]["MR"].asDouble();
	loss = root["WeakLearner"]["params"]["loss"].asDouble();
}
