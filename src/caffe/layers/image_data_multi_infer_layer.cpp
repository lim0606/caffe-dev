#include <opencv2/core/core.hpp>

#include <fstream>  // NOLINT(readability/streams)
#include <iostream>  // NOLINT(readability/streams)
#include <string>
#include <utility>
#include <vector>

#include "caffe/data_layers.hpp"
#include "caffe/layer.hpp"
#include "caffe/util/benchmark.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/util/rng.hpp"

namespace caffe {

template <typename Dtype>
ImageDataMultipleInferenceLayer<Dtype>::~ImageDataMultipleInferenceLayer<Dtype>() {
  this->JoinPrefetchThread();
}

template <typename Dtype>
void ImageDataMultipleInferenceLayer<Dtype>::DataLayerSetUp(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
//  tmp_ = 0; 
  const int new_height = this->layer_param_.image_data_multi_infer_param().new_size();//this->layer_param_.image_data_param().new_height();
  const int new_width  = this->layer_param_.image_data_multi_infer_param().new_size();//this->layer_param_.image_data_param().new_width();
  const bool is_color  = this->layer_param_.image_data_multi_infer_param().is_color();
  string root_folder = this->layer_param_.image_data_multi_infer_param().root_folder();

  CHECK((new_height == 0 && new_width == 0) ||
      (new_height > 0 && new_width > 0)) << "Current implementation requires "
      "new_height and new_width to be set at the same time.";
  // Read the file with filenames and labels
  const string& source = this->layer_param_.image_data_multi_infer_param().source();
  LOG(INFO) << "Opening file " << source;
  std::ifstream infile(source.c_str());
  string filename;
  int label;
  while (infile >> filename >> label) {
    lines_.push_back(std::make_pair(filename, label));
  }

  if (this->layer_param_.image_data_multi_infer_param().shuffle()) {
    // randomly shuffle data
    LOG(INFO) << "Shuffling data";
    const unsigned int prefetch_rng_seed = caffe_rng_rand();
    prefetch_rng_.reset(new Caffe::RNG(prefetch_rng_seed));
    ShuffleImages();
  }
  LOG(INFO) << "A total of " << lines_.size() << " images.";

  lines_id_ = 0;
  // Check if we would need to randomly skip a few data points
  if (this->layer_param_.image_data_multi_infer_param().rand_skip()) {
    unsigned int skip = caffe_rng_rand() %
        this->layer_param_.image_data_multi_infer_param().rand_skip();
    LOG(INFO) << "Skipping first " << skip << " data points.";
    CHECK_GT(lines_.size(), skip) << "Not enough points to skip";
    lines_id_ = skip;
  }
  // Read an image, and use it to initialize the top blob.
  cv::Mat cv_img = ReadImageToCVMat(root_folder + lines_[lines_id_].first,
                                    new_height, new_width, is_color);  
  const int channels = cv_img.channels();
  const int height = cv_img.rows;
  const int width = cv_img.cols;
  // image
  const int crop_size = this->layer_param_.transform_param().crop_size();
  const int batch_size = this->layer_param_.image_data_multi_infer_param().batch_size();
  const int multi_infer_size = this->layer_param_.image_data_multi_infer_param().multi_infer_size();
  const int num_images = batch_size * multi_infer_size; 
  if (crop_size > 0) {
    top[0]->Reshape(/*batch_size*/num_images, channels, crop_size, crop_size);
    this->prefetch_data_.Reshape(/*batch_size*/num_images, channels, crop_size, crop_size);
    this->transformed_data_.Reshape(1, channels, crop_size, crop_size);
  } else {
    top[0]->Reshape(/*batch_size*/num_images, channels, height, width);
    this->prefetch_data_.Reshape(/*batch_size*/num_images, channels, height, width);
    this->transformed_data_.Reshape(1, channels, height, width);
  }
  LOG(INFO) << "output data size: " << top[0]->num() << ","
      << top[0]->channels() << "," << top[0]->height() << ","
      << top[0]->width();
  // label
  //top[1]->Reshape(/*batch_size*/num_images, 1, 1, 1);
  //this->prefetch_label_.Reshape(/*batch_size*/num_images, 1, 1, 1);
  top[1]->Reshape(batch_size, 1, 1, 1);
  this->prefetch_label_.Reshape(batch_size, 1, 1, 1);

}

template <typename Dtype>
void ImageDataMultipleInferenceLayer<Dtype>::ShuffleImages() {
  caffe::rng_t* prefetch_rng =
      static_cast<caffe::rng_t*>(prefetch_rng_->generator());
  shuffle(lines_.begin(), lines_.end(), prefetch_rng);
}

// This function is used to create a thread that prefetches the data.
template <typename Dtype>
void ImageDataMultipleInferenceLayer<Dtype>::InternalThreadEntry() {
//  tmp_++; 
  CPUTimer batch_timer;
  batch_timer.Start();
  double read_time = 0;
  double trans_time = 0;
  CPUTimer timer;
  CHECK(this->prefetch_data_.count());
  CHECK(this->transformed_data_.count());
  Dtype* top_data = this->prefetch_data_.mutable_cpu_data();
  Dtype* top_label = this->prefetch_label_.mutable_cpu_data();
  ImageDataMultipleInferenceParameter image_data_multi_infer_param = this->layer_param_.image_data_multi_infer_param();
  //const int batch_size = image_data_multi_infer_param.batch_size();
  const int batch_size = image_data_multi_infer_param.batch_size();
  const int multi_infer_size = image_data_multi_infer_param.multi_infer_size(); 
  const int num_images = batch_size * multi_infer_size; 
  const int new_height = image_data_multi_infer_param.new_size();//image_data_param.new_height();
  const int new_width = image_data_multi_infer_param.new_size();//image_data_param.new_width();
  const bool is_color = image_data_multi_infer_param.is_color();
  string root_folder = image_data_multi_infer_param.root_folder();

  // datum scales
  const int lines_size = lines_.size();
  int item_id = 0; 
  for (int batch_id = 0; batch_id < batch_size; ++batch_id) {
    // read image

    // get a blob
    timer.Start();
    CHECK_GT(lines_size, lines_id_);
    //cv::Mat cv_img = ReadImageToCVMat(root_folder + lines_[lines_id_].first,
    //                                new_height, new_width, is_color);
    cv::Mat cv_img = ReadImageToCVMat(root_folder + lines_[lines_id_].first, 
                                      0, 0, is_color); 
    if (!cv_img.data) {
      continue;
    }

    for (int multi_infer_id = 0; multi_infer_id < multi_infer_size; ++multi_infer_id) {
      // assign to dataum
      read_time += timer.MicroSeconds();
      timer.Start();
      // Apply transformations (mirror, crop...) to the image
      int offset = this->prefetch_data_.offset(item_id);
      this->transformed_data_.set_cpu_data(top_data + offset);
      //this->data_transformer_.Transform(cv_img, &(this->transformed_data_));
      this->data_transformer_.TransformAffine(cv_img, new_height, new_width, &(this->transformed_data_));  
      //this->data_transformer_.TransformAffine(cv_img, new_height, new_width, &(this->transformed_data_), tmp_, item_id); 
      trans_time += timer.MicroSeconds();

      //label
      //top_label[item_id] = lines_[lines_id_].second;

      // increase item_id
      item_id++; 
    }

    //label
    top_label[batch_id] = lines_[lines_id_].second;

    // go to the next iter
    lines_id_++;
    if (lines_id_ >= lines_size) {
      // We have reached the end. Restart from the first.
      DLOG(INFO) << "Restarting data prefetching from start.";
      lines_id_ = 0;
      if (this->layer_param_.image_data_multi_infer_param().shuffle()) {
        ShuffleImages();
      }
    }
  } 
  CHECK_EQ(num_images, item_id); // just in case

/*  for (int item_id = 0; item_id < batch_size; ++item_id) {
    // get a blob
    timer.Start();
    CHECK_GT(lines_size, lines_id_);
    //cv::Mat cv_img = ReadImageToCVMat(root_folder + lines_[lines_id_].first,
    //                                new_height, new_width, is_color);
    cv::Mat cv_img = ReadImageToCVMat(root_folder + lines_[lines_id_].first, 
                                      0, 0, is_color); 
    if (!cv_img.data) {
      continue;
    }
    read_time += timer.MicroSeconds();
    timer.Start();
    // Apply transformations (mirror, crop...) to the image
    int offset = this->prefetch_data_.offset(item_id);
    this->transformed_data_.set_cpu_data(top_data + offset);
    //this->data_transformer_.Transform(cv_img, &(this->transformed_data_));
    this->data_transformer_.TransformAffine(cv_img, new_height, new_width, &(this->transformed_data_)); 
    trans_time += timer.MicroSeconds();

    top_label[item_id] = lines_[lines_id_].second;
    // go to the next iter
    lines_id_++;
    if (lines_id_ >= lines_size) {
      // We have reached the end. Restart from the first.
      DLOG(INFO) << "Restarting data prefetching from start.";
      lines_id_ = 0;
      if (this->layer_param_.image_data_multi_infer_param().shuffle()) {
        ShuffleImages();
      }
    }
  }*/
  batch_timer.Stop();
  DLOG(INFO) << "Prefetch batch: " << batch_timer.MilliSeconds() << " ms.";
  DLOG(INFO) << "     Read time: " << read_time / 1000 << " ms.";
  DLOG(INFO) << "Transform time: " << trans_time / 1000 << " ms.";
}

INSTANTIATE_CLASS(ImageDataMultipleInferenceLayer);
REGISTER_LAYER_CLASS(IMAGE_DATA_MULTIPLE_INFERENCE, ImageDataMultipleInferenceLayer);
}  // namespace caffe
