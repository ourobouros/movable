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

#ifndef DATASET_HPP_
#define DATASET_HPP_

#include <iostream>
#include <vector>
#include <iomanip>

#pragma GCC diagnostic ignored "-Wsign-conversion"
#pragma GCC diagnostic ignored "-Wcast-qual"
#include <Eigen/Core>
#include <Eigen/Dense>
#include <opencv2/opencv.hpp>
#include <opencv2/core/eigen.hpp>
#pragma GCC diagnostic pop
#pragma GCC diagnostic pop

extern "C" {
#include <omp.h>
}

#include "DataTypes.hpp"
#include "logging.hpp"
#include "utils.hpp"
#include "Parameters.hpp"

#ifdef MOVABLE_TRAIN
const int POS_GT_CLASS = 1;
const int NEG_GT_CLASS = -1;
const int IGN_GT_CLASS = 0;
#endif /* MOVABLE_TRAIN */

const int MASK_EXCLUDED = 0;
const int MASK_INCLUDED = 255;

const int FEEDBACK_SAMPLE_WEIGHT = 10;

class BoostedClassifier;

/**
 * class Dataset - Represent a dataset with all associated images and paths
 *
 * @data          : set of data vectors (one vector per channel)
 * @dataChNo      : number of data channels
 * @imageOps      : set of operations performed on the loaded images (each
 *                  operation will lead to an additional channel)
 * @imagesNo      : number of loaded images
 * @imageNames    : names of the loaded images
 * @sampleSize    : size of a sampled patch
 * @borderSize    : size of the replicated border around the image
 * @masks         : image masks
 * @gts           : set of ground-truth vectors (one vector per gt pair)
 * @originalGts   : original ground-truth images (used for final classification)
 * @gtValues      : values available in gt
 * @gtPairValues  : gt pairs numeric values (each pair will require a boosted
 *                  classifier)
 * @gtPairsNo     : number of ground-truth pairs
 */
class Dataset {
public:
	/**
	 * Dataset() - Initialize the data channels, then load the data
	 *
	 * @params      : parameters used for dataset initialization
	 */
	Dataset(const Parameters &params);

	/**
	 * Dataset() - Create a new dataset where the channels are those
	 *             inherited by the source dataset complemented with the
	 *             results obtained by applying the specified boosted
	 *             classifiers to these images
	 *
	 * @params            : simulation's parameters
	 * @srcDataset        : source dataset
	 * @boostedClassifiers: classifiers used for the generation of the
	 *                      additional channels
	 */
#ifndef TESTS
#ifdef MOVABLE_TRAIN
	Dataset(const Parameters &params,
		const Dataset &srcDataset,
		const std::vector< BoostedClassifier * > &boostedClassifiers);
#else
	Dataset(const Dataset &srcDataset,
		const std::vector< BoostedClassifier * > &boostedClassifiers);
#endif /* MOVABLE_TRAIN */
#else
	Dataset(const Dataset &srcDataset,
		const std::vector< BoostedClassifier * > &boostedClassifiers);
#endif /* TESTS */

#ifdef MOVABLE_TRAIN
	/**
	 * getGt() - Get a reference to a specific ground-truth for the
	 *           specified gt pair
	 *
	 * @pairNo : image pair to which the ground-truth belongs to
	 * @imageNo: number of the image for which to return the ground-truth
	 *
	 * Return: reference to the specified gt if available, reference to an
	 *         empty EMat otherwise
	 */
	const EMat& getGt(const int pairNo,
			  const unsigned int imageNo) const
	{
		if (pairNo < 0 || pairNo >= (int)gtPairsNo ||
		    imageNo >= imagesNo) {
			log_err("The requested gt %d does not exist in "
				"pair %d (limits: image = %d, pair = %d)",
				imageNo, pairNo, imagesNo-1, gtPairsNo-1);
			/* Return an empty EMat */
			static EMat nullresult;
			return nullresult;
		}
		return gts[pairNo][imageNo];
	}

	/**
	 * getGtNegativePairValue() - Get the value of the negative class for a
	 *                            given gt pair
	 *
	 * @gtPair: considered gt pair
	 *
	 * Return: Numerical value of the negative class for the given gt pair
	 */
	int getGtNegativePairValue(const unsigned int gtPair) const
	{
		if (gtPair >= gtPairsNo) {
			log_err("The desired gt pair (%d) does not exist",
				gtPair);
			return -1;
		}
		return gtPairValues[gtPair].first;
	}

	/**
	 * getGtPositivePairValue() - Get the value of the positive class for a
	 *                            given gt pair
	 *
	 * @gtPair: considered gt pair
	 *
	 * Return: Numerical value of the positive class for the given gt pair
	 */
	int getGtPositivePairValue(const unsigned int gtPair) const
	{
		if (gtPair >= gtPairsNo) {
			log_err("The desired gt pair (%d) does not exist",
				gtPair);
			return -1;
		}
		return gtPairValues[gtPair].second;
	}

	/**
	 * getGtPairsNo() - Return the number of ground-truth pairs available
	 *
	 * Return: number of available gt pairs
	 */
	unsigned int getGtPairsNo() const { return gtPairsNo; };

	/**
	 * getGtValuesNo() - Return the number of ground-truth values considered
	 *
	 * Return: number of considered gt values
	 */
	unsigned int getGtValuesNo() const { return gtValues.size(); };

	/**
	 * getGtVector() - Get a reference to a the specified gt pair vector
	 *
	 *
	 * @pairNo: number of the desired gt pair
	 *
	 * Return: reference to the specified gt vector if set, reference to an
	 *         empty one otherwise
	 */
	const gtVector& getGtVector(const int pairNo) const
	{
		if (pairNo >= (int)gtPairsNo || pairNo < 0) {
			log_err("The requested gt vector does not exist (pair "
				"= %d, max value = %d)", pairNo, gtPairsNo-1);
			/* Return an empty vector */
			static gtVector nullresult;
			return nullresult;
		}
		return gts[pairNo];
	}

	/**
	 * getOriginalGt() - Get a reference to a specific original ground-truth
	 *                   image
	 *
	 * @imageNo: number of the image for which to return the ground-truth
	 *
	 * Return: reference to the specified gt if available, reference to an
	 *         empty EMat otherwise
	 */
	const EMat& getOriginalGt(const unsigned int imageNo) const
	{
		if (imageNo >= imagesNo) {
			log_err("The requested original gt (%d) does not exist",
				imageNo);
			/* Return an empty EMat */
			static EMat nullresult;
			return nullresult;
		}
		return originalGts[imageNo];
	}

	/**
	 * getSampleMatrix() - Fill a matrix with a specified set of samples
	 *
	 * @samplePositions: list of available sampling positions
	 * @samplesIdx     : indexes of the subset of patches to sample
	 * @chNo           : channel from which to sample from
	 * @rowOffset      : row offset to impose while sampling
	 * @colOffset      : col offset to impose while sampling
	 * @size           : size of the samples to extract
	 *
	 * @samples        : resulting matrix filled with the extracted samples
	 *
	 * Samples are taken from the given upper-left corner plus the offsets.
	 * Samples are row-major (that is, each sample is taken row-by-row).
	 */
	void getSampleMatrix(const sampleSet &samplePositions,
			     const std::vector< unsigned int > &samplesIdx,
			     const unsigned int chNo,
			     const unsigned int rowOffset,
			     const unsigned int colOffset,
			     const unsigned int size,
			     EMat &samples) const
	{
		assert (chNo < dataChNo);

		const unsigned int samplesNo = samplesIdx.size();
		const unsigned int sampleArea = size*size;

		samples.resize(samplesNo, sampleArea);
		for (unsigned int iX = 0; iX < samplesNo; ++iX) {
			const samplePos &s = samplePositions[samplesIdx[iX]];
			EMat tmp = data[chNo][s.imageNo].block(s.row+rowOffset,
							       s.col+colOffset,
							       size, size);
			samples.row(iX) = Eigen::Map< EMat >(tmp.data(), 1,
							     sampleArea);
		}
	}

	/**
	 * getSamplePositions() - Get a set of sampling positions from the
	 *                        dataset
	 *
	 * @sampleClass    : class to sample (either POS_GT_CLASS or
	 *                   NEG_GT_CLASS)
	 * @gtPair         : specify which gt pair has to be considered
	 * @samplesNo      : number of samples requested
	 *
	 * @samplePositions: output sampled positions
	 *
	 * Return: Number of sampling positions collected on success,
	 *         -EXIT_FAILURE otherwise
	 */
	int getSamplePositions(const int sampleClass,
			       const unsigned int gtPair,
			       const unsigned int samplesNo,
			       sampleSet &samplePositions) const
	{
		if (sampleClass != POS_GT_CLASS && sampleClass != NEG_GT_CLASS) {
			return -EXIT_FAILURE;
		}

		return getAvailableSamples(gts[(unsigned int)gtPair],
					   sampleClass,
					   samplesNo,
					   samplePositions);
	}

	/**
	 * isFeedbackImage() - Returns whether an image given as a parameter has
	 *                     been returned by a technician as feedback or not
	 *                     (this increases the weight of its samples)
	 *
	 * @imageNo: number of the image to check
	 *
	 * Return: true if the image is a feedback one, false otherwise
	 */
	bool isFeedbackImage(const int imageNo) const
	{
		assert(imageNo >= 0);
		assert(imageNo < imagesNo);

		return feedbackImagesFlag[imageNo];
	}

	/**
	 * shrinkSamplePositions() - Drop samples from a sample set until the
	 *                           desired size is reached
	 *
	 * @samplePositions: considered sampling points
	 * @desiredSize    : desired size of the sampling set
	 */
	static void shrinkSamplePositions(sampleSet &samplePositions,
					  const unsigned int desiredSize)
	{
		assert(desiredSize > 0);
		assert(desiredSize <= samplePositions.size());

		std::vector< unsigned int > newSamplesPos =
			randomSamplingWithoutReplacement(desiredSize,
							 samplePositions.size());

		sampleSet newSamples(desiredSize);

		for (unsigned int i = 0; i < desiredSize; ++i) {
			newSamples[i] = samplePositions[newSamplesPos[i]];
		}

		samplePositions = newSamples;
	}

#endif
	/**
	 * getBorderSize() - Return the size of the border
	 *
	 * Return: size of the border
	 */
	unsigned int getBorderSize() const { return borderSize; };

	/**
	 * getChsForImage() - Get the channels corresponding to a specified
	 *                    image in OpenCV format and enlarged by borderSize
	 *
	 * @n  : image number
	 *
	 * @chs: resulting vector containing the desired data in OpenCV format
	 */
	void getChsForImage(const unsigned int n,
			    std::vector< cv::Mat > &chs) const;

	/**
	 * getData() - Get a reference to the data of a specific image-channel
	 *             pair
	 *
	 * @channelNo: number of the desired channel
	 * @imageNo  : number of the desired image
	 *
	 * Return: reference to the specified data if available, reference to an
	 *         empty EMat otherwise
	 */
	const EMat& getData(const unsigned int channelNo,
			    const unsigned int imageNo) const
	{
		if (channelNo >= dataChNo || imageNo >= imagesNo) {
			log_err("The requested image %d does not exist in "
				"channel %d (limits: image = %d, channel = %d)",
				imageNo, channelNo, imagesNo-1, dataChNo-1);
			/* Return an empty EMat */
			static EMat nullresult;
			return nullresult;
		}
		return data[channelNo][imageNo];
	}

	/**
	 * getDataChNo() - Return the number of data channels available
	 *
	 * Return: number of available data channels
	 */
	unsigned int getDataChNo() const { return dataChNo; };

	/**
	 * getDataVector() - Get a reference to the specific data channel
	 *                   vector
	 *
	 * @channelNo: number of the desired channel
	 *
	 * Return: reference to the desired channel if available, reference to
	 *         an empty one otherwise
	 */
	const dataVector& getDataVector(const unsigned int channelNo) const
	{
		if (channelNo >= dataChNo) {
			log_err("The requested data vector does not exist "
				"channel = %d, max value = %d)",
				channelNo, dataChNo-1);
			/* Return an empty vector if the wrong channel number is
			   requested */
			static dataVector nullresult;
			return nullresult;
		}
		return data[channelNo];
	}

	/**
	 * getImageName() - Return the filename of an input image
	 *
	 * @imageNo  : number of the desired image
	 *
	 * Return: filename of the desired image if found, an empty string
	 *         otherwise
	 */
	std::string getImageName(const unsigned int imageNo) const
	{
		if (imageNo >= imagesNo) {
			log_err("The requested image %d does not exist",
				imageNo);
			return std::string();
		}
		return imageNames[imageNo];
	}

	/**
	 * getImagesNo() - Return the number of images currently loaded
	 *
	 * Return: number of images loaded in the dataset
	 */
	unsigned int getImagesNo() const { return imagesNo; };

	/**
	 * getMask() - Return the desired image mask
	 *
	 * @imageNo: number of the image whose mask is requested
	 *
	 * Return: reference to the desired mask if available, reference to
	 *         an empty EMat otherwise
	 */
	const EMat& getMask(const unsigned int imageNo) const
	{
		if (imageNo >= imagesNo) {
			log_err("The requested image %d does not exist "
				"(limit: image = %d)",
				imageNo, imagesNo-1);
			/* Return an empty EMat */
			static EMat nullresult;
			return nullresult;
		}
		return masks[imageNo];
	}

	/**
	 * getSampleSize() - Get the size of a sample extracted from the dataset
	 *
	 * @Return: Size of the considered samples
	 */
	unsigned int getSampleSize() const
	{
		return sampleSize;
	}

private:
	typedef int (*ImageOps)(const cv::Mat &src, EMat &dst,
				const void *opaque);

	dataChannels data;
	unsigned int dataChNo;
	std::vector< ImageOps > imageOps;
	unsigned int imagesNo;
	std::vector< std::string > imageNames;
	std::vector< bool > feedbackImagesFlag;

	unsigned int sampleSize;
	unsigned int borderSize;

	maskVector masks;

#ifdef MOVABLE_TRAIN
	gtPairs gts;
	gtVector originalGts;
	std::vector< int > gtValues;
	std::vector< std::pair< int, int > > gtPairValues;
	unsigned int gtPairsNo;

	/**
	 * addGt() - Preprocess the ground-truth image passed as parameter, and
	 *           then push it into the dataset, exploding it on the
	 *           different gt pairs
	 *
	 * @src: input ground-truth image
	 *
	 * Return: EXIT_SUCCESS
	 */
	int addGt(const cv::Mat &src);

	/**
	 * addImage() - Add an image along with its ground-truth and mask,
	 *              computing the additional channels from the image itself
	 *
	 * @imgPath : path of the input image
	 * @maskPath: path of the input mask
	 * @gtPath  : path of the input ground-truth
	 *
	 * Return: -EXIT_FAILURE if one of more of the input paths are invalid
	 *         or an image has invalid size, EXIT_SUCCESS otherwise
	 */

	int addImage(const std::string &imgPath,
		     const std::string &maskPath,
		     const std::string &gtPath);

	/*
	 * collectAllSamplePositions() - Collect all the possible samples of the
	 *                               given class from the specified gt
	 *                               vector
	 *
	 * @gt               : vector of gt images used in sampling
	 * @sampleClass      : identifier of the class to sample
	 *
	 * @availableSamples : returned vector of samples, where samples are
	 *                     split over images
	 * @samplesPerImageNo: number of samples of the desired class for each
	 *                     separate image
	 *
	 * Return: total number of sampled points
	 */
	unsigned int
	collectAllSamplePositions(const gtVector &gt,
				  const int sampleClass,
				  std::vector< sampleSet > &availableSamples,
				  std::vector< int > &samplesPerImageNo) const;

	/**
	 * createGtPairs() - Create a set of gt pairs, starting from a list of
	 *                   values
	 *
	 * @gtValues: set of desired ground-truth values to classify
	 */
	void createGtPairs(std::vector< int > gtValues);

	/**
	 * getAvailableSamples() - Extract a set of samples of the desired size
	 *                         from the given gt vector
	 *
	 * @gt             : vector of gt images used in sampling
	 * @sampleClass    : identifier of the class to sample
	 * @samplesNo      : requested number of samples
	 *
	 * @samplePositions: output sampled positions
	 *
	 * Return: total number of sampled points
	 */
	int getAvailableSamples(const gtVector &gt,
				const int sampleClass,
				const unsigned int samplesNo,
				sampleSet &samplePositions) const;

	/**
	 * loadPaths() - Load a list of paths for imgs/masks/gts
	 *
	 * @params    : simulation's parameters
	 *
	 * @img_paths : output list of image paths
	 * @mask_paths: output list of mask paths
	 * @gt_paths  : output list of gt paths
	 *
	 * Return: -EXIT_FAILURE on error, EXIT_SUCCESS otherwise
	 */
	int loadPaths(const Parameters &params,
		      std::vector< std::string > &img_paths,
		      std::vector< std::string > &mask_paths,
		      std::vector< std::string > &gt_paths);

#else
	/**
	 * addImage() - Add an image along with its mask, computing the
	 *              additional channels from the image itself
	 *
	 * @imgPath : path of the input image
	 * @maskPath: path of the input mask
	 *
	 * Return: -EXIT_FAILURE if one of more of the input paths are invalid
	 *         or an image has invalid size, EXIT_SUCCESS otherwise
	 */

	int addImage(const std::string &imgPath,
		     const std::string &maskPath);

	/**
	 * loadPaths() - Load a list of paths for imgs/masks
	 *
	 * @params    : simulation's parameters
	 *
	 * @img_paths : output list of image paths
	 * @mask_paths: output list of mask paths
	 *
	 * Return: -EXIT_FAILURE on error, EXIT_SUCCESS otherwise
	 */
	int loadPaths(const Parameters &params,
		      std::vector< std::string > &img_paths,
		      std::vector< std::string > &mask_paths);
#endif /* MOVABLE_TRAIN */

	/**
	 * imageGrayCh() - Process an image, converting it to grayscale and
	 *                 pushing it into the corresponding channel of the
	 *                 dataset
	 *
	 * @src   : input image
	 * @dst   : converted image
	 * @opaque: opaque pointer to useful data (here: border size)
	 *
	 * Return: EXIT_SUCCESS
	 */
	static int imageGrayCh(const cv::Mat &src, EMat &dst,
			       const void *opaque);

	/**
	 * imageGreenCh() - Process an image, taking the green channel only and
	 *                  pushing it into the corresponding channel of the
	 *                  dataset
	 *
	 * @src   : input image
	 * @dst   : converted image
	 * @opaque: opaque pointer to useful data (here: border size)
	 *
	 * Return: EXIT_SUCCESS
	 */
	static int imageGreenCh(const cv::Mat &src, EMat &dst,
				const void *opaque);

	/**
	 * imageRedCh() - Process an image, taking the red channel only and
	 *                pushing it into the corresponding channel of the
	 *                dataset
	 *
	 * @src   : input image
	 * @dst   : converted image
	 * @opaque: opaque pointer to useful data (here: border size)
	 *
	 * Return: EXIT_SUCCESS
	 */
	static int imageRedCh(const cv::Mat &src, EMat &dst,
			      const void *opaque);

	/**
	 * gaussianFiltering() - Process an image, taking its green channel and
	 *                       passing it through a Gaussian filter with sigma
	 *                       1
	 *
	 * @src   : input image
	 * @dst   : converted image
	 * @opaque: opaque pointer to useful data (here: border size)
	 *
	 * Return: EXIT_SUCCESS
	 */
	static int gaussianFiltering(const cv::Mat &src, EMat &dst,
				     const void *opaque);

	/**
	 * laplacianFiltering() - Process an image, taking its green channel and
	 *                        passing it through a Laplacian filter of size 9
	 *
	 * @src   : input image
	 * @dst   : converted image
	 * @opaque: opaque pointer to useful data (here: border size)
	 *
	 * Return: EXIT_SUCCESS
	 */
	static int laplacianFiltering(const cv::Mat &src, EMat &dst,
				      const void *opaque);

	/**
	 * medianFiltering() - Process an image, converting it to grayscale and
	 *                     then passing it through a median filter with 3
	 *                     pixel of radius (1.5 would have been better, but
	 *                     must be > 1, odd, and not fractional in OpenCV)
	 *
	 * @src   : input image
	 * @dst   : converted image
	 * @opaque: opaque pointer to useful data (here: border size)
	 *
	 * Return: EXIT_SUCCESS
	 */
	static int medianFiltering(const cv::Mat &src, EMat &dst,
				   const void *opaque);

	/**
	 * sobelDrvX() - Process an image, taking its green channel and
	 *               passing it through a Sobel derivative filter in the X
	 *               direction
	 *
	 * @src   : input image
	 * @dst   : converted image
	 * @opaque: opaque pointer to useful data (here: border size)
	 *
	 * Return: EXIT_SUCCESS
	 */
	static int sobelDrvX(const cv::Mat &src, EMat &dst,
				      const void *opaque);

	/**
	 * sobelDrvY() - Process an image, taking its green channel and
	 *               passing it through a Sobel derivative filter in the Y
	 *               direction
	 *
	 * @src   : input image
	 * @dst   : converted image
	 * @opaque: opaque pointer to useful data (here: border size)
	 *
	 * Return: EXIT_SUCCESS
	 */
	static int sobelDrvY(const cv::Mat &src, EMat &dst,
				      const void *opaque);

	/**
	 * addMask() - Preprocess the mask passed as parameter, and then push it
	 *             into the dataset
	 *
	 * @src: input image mask
	 *
	 * Return: EXIT_SUCCESS
	 */
	int addMask(cv::Mat &src);

	/**
	 * loadPathFile() - Load a list of paths from a file
	 *
	 * @dataset_path: path of the dataset
	 * @fname       : name of the file containing the path list
	 *
	 * @loaded_paths: output list of file paths
	 *
	 * Return: -EXIT_FAILURE on error, EXIT_SUCCESS OTHERWISE
	 */
	int loadPathFile(const std::string &dataset_path,
			 const std::string &fname,
			 std::vector< std::string > &loaded_paths);

};

#endif /* DATASET_HPP_ */