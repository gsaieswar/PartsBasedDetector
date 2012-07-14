/* 
 *  Software License Agreement (BSD License)
 *
 *  Copyright (c) 2012, Willow Garage, Inc.
 *  All rights reserved.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above
 *     copyright notice, this list of conditions and the following
 *     disclaimer in the documentation and/or other materials provided
 *     with the distribution.
 *   * Neither the name of Willow Garage, Inc. nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 *  FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 *  COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 *  BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 *  LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 *  CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 *  LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN
 *  ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 *  POSSIBILITY OF SUCH DAMAGE.
 *
 *  File:    MatlabIOModel.hpp
 *  Author:  Hilton Bristow
 *  Created: Jun 30, 2012
 */

#ifndef MATLABIOMODEL_HPP_
#define MATLABIOMODEL_HPP_
#include <MatlabIO.hpp>
#include "Model.hpp"
#include "types.hpp"

/*
 *
 */
class MatlabIOModel: public Model {
public:
	MatlabIOModel() {}
	virtual ~MatlabIOModel() {}

	//! convert a vector of integers from Matlab 1-based indexing to C++ 0-based indexing
	void zeroIndex(vectori& idx) {
		for (int n = 0; n < idx.size(); ++n) idx[n] -= 1;
	}

	//! convert an integer from Matlab 1-based indexing to C++ 0-based indexing
	void zeroIndex(int& idx) {
		idx -= 1;
	}

	//! convert a vector of Point from Matlab 1-based indexing to C++ 0-based indexing
	void zeroIndex(vectorPoint& pt) {
		cv::Point one(1,1);
		for (int n = 0; n < pt.size(); ++n) pt[n] = pt[n] - one;
	}

	/*! @brief deserialize a Matlab .Mat file into memory
	 *
	 * deserialize a valid version 5 .Mat file using the underlying
	 * MatlabIO parser, and populate the model fields. If any of the fields
	 * do not exist, or a bad type cast is attempted, an exception will be thrown
	 *
	 * @param filename the path to the model file
	 * @return true if the file was found, opened and verified to be a valid Matlab
	 * version 5 file
	 * @throws boost::bad_any_cast, exception
	 */
	bool deserialize(std::string filename) {

		// open the Mat File for reading
		MatlabIO cvmatio;
		bool ok = cvmatio.open(filename, "r");
		if (!ok) return false;

		// read all of the variables from the file
		std::vector<MatlabIOContainer> variables;
		variables = cvmatio.read();

		// populate the model, one variable at a time
		name_ = cvmatio.find<std::string>(variables, "name");

		cv::Mat pa = cvmatio.find<cv::Mat>(variables, "pa");
		for (int n = 0; n < pa.cols*pa.rows; ++n) conn_.push_back(pa.at<double>(n));
		zeroIndex(conn_);

		//model
		vectorMatlabIOContainer model = cvmatio.find<vector2DMatlabIOContainer>(variables, "model")[0];

		nscales_ = cvmatio.find<double>(model, "interval");
		thresh_  = -2.0f;//cvmatio.find<double>(model, "thresh");
		binsize_ = cvmatio.find<double>(model, "sbin");
		norient_ = 18;

		// ------------------------------
		// get the filters
		vector2DMatlabIOContainer filters = cvmatio.find<vector2DMatlabIOContainer>(model, "filters");
		for (int f = 0; f < filters.size(); ++f) {
			// flatten the filters to 2D
			cv::Mat filter = cvmatio.find<cv::Mat>(filters[f], "w");
			const int M = filter.rows;
			const int N = filter.cols;
			vectorMat filter_vec;
			cv::split(filter, filter_vec);
			const int C = filter_vec.size();
			flen_ = C;
			cv::Mat filter_flat(cv::Size(N * C, M), cv::DataType<double>::type);
			for (int m = 0; m < M; ++m) {
				for (int c = 0; c < C; ++c) {
					for (int n = 0; n < N; ++n) {
						filter_flat.at<double>(m,n*C+c) = filter_vec[c].at<double>(m,n);
					}
				}
			}
			filtersw_.push_back(filter_flat);
			filtersi_.push_back(cvmatio.find<double>(filters[f], "i"));
		}

		// ------------------------------
		// get the components
		vectorMatlabIOContainer components = cvmatio.find<vectorMatlabIOContainer>(model, "components");
		int ncomponents = components.size();
		biasid_.resize(ncomponents);
		filterid_.resize(ncomponents);
		defid_.resize(ncomponents);
		parentid_.resize(ncomponents);
		for (int c = 0; c < ncomponents; ++c) {
			// a single component is a struct array
			vector2DMatlabIOContainer component = components[c].data<vector2DMatlabIOContainer>();
			int nparts = component.size();
			biasid_[c].resize(nparts);
			filterid_[c].resize(nparts);
			defid_[c].resize(nparts);
			parentid_[c].resize(nparts);

			// for each element, add to the component indices
			for (int p = 0; p < nparts; ++p) {
				cv::Mat defid = cvmatio.find<cv::Mat>(component[p], "defid");
				cv::Mat filterid = cvmatio.find<cv::Mat>(component[p], "filterid");
				int parentid = cvmatio.find<double>(component[p], "parent");

				// the biasid type can change, depending on the number of elements saved
				MatlabIOContainer biasid = cvmatio.find(component[p], "biasid");
				if (biasid.typeEquals<double>()) {
					biasid_[c][p] = std::vector<int>(1, biasid.data<double>());
					zeroIndex(biasid_[c][p]);
				} else {
					cv::Mat biasidmat = biasid.data<cv::Mat>();
					biasid_[c][p] = std::vector<int>(biasidmat.begin<double>(), biasidmat.end<double>());
					zeroIndex(biasid_[c][p]);
				}

				parentid_[c][p] = parentid;
				filterid_[c][p] = std::vector<int>(filterid.begin<double>(), filterid.end<double>());
				defid_[c][p]    = std::vector<int>(defid.begin<double>(),    defid.end<double>());

				// re-index from zero (Matlab uses 1-based indexing)
				zeroIndex(parentid_[c][p]);
				zeroIndex(filterid_[c][p]);
				zeroIndex(defid_[c][p]);
			}
		}

		// ------------------------------
		// get the defs
		vector2DMatlabIOContainer defs = cvmatio.find<vector2DMatlabIOContainer>(model, "defs");
		int ndefs = defs.size();
		for (int n = 0; n < ndefs; ++n) {
			defw_.push_back(cvmatio.find<cv::Mat>(defs[n], "w"));
			defi_.push_back(cvmatio.find<double>(defs[n], "i"));
			cv::Mat anchor = cvmatio.find<cv::Mat>(defs[n], "anchor");
			anchors_.push_back(cv::Point(anchor.at<double>(0), anchor.at<double>(1)));
		}
		zeroIndex(anchors_);

		// ------------------------------
		// get the bias
		vector2DMatlabIOContainer bias = cvmatio.find<vector2DMatlabIOContainer>(model, "bias");
		int nbias = bias.size();
		for (int n = 0; n < nbias; ++n) {
			biasw_.push_back(cvmatio.find<double>(bias[n], "w"));
			biasi_.push_back(cvmatio.find<double>(bias[n], "i"));
		}

		return true;
	}
};

#endif /* MATLABIOMODEL_HPP_ */
