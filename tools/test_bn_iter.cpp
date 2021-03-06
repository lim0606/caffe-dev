
//Jaehyun Lim
#include <glog/logging.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>
#include <fstream> 

#include "hdf5.h"
#include "leveldb/db.h"
#include "lmdb.h"

#include "caffe/caffe.hpp"
#include "caffe/proto/caffe.pb.h"
#include "caffe/util/upgrade_proto.hpp"

#include <boost/shared_ptr.hpp>
#include <boost/pointer_cast.hpp>

using caffe::Blob;
using caffe::Caffe;
using caffe::Net;
using caffe::Layer;
using caffe::BNLayer;
using caffe::shared_ptr;
using caffe::Timer;
using caffe::vector;
using caffe::LayerParameter_LayerType_BN; 
using caffe::caffe_set; 
using caffe::NetParameter; 
using boost::dynamic_pointer_cast;

// Define flags
DEFINE_int32(gpu, -1,
    "Run in GPU mode on given device ID.");
DEFINE_string(solver, "",
    "The solver definition protocol buffer text file.");
DEFINE_string(train_model, "",
    "The model definition protocol buffer text file..");
DEFINE_string(test_model, "",
    "The model definition protocol buffer text file..");
//DEFINE_string(snapshot, "",
//    "The snapshot solver state to resume training.");
DEFINE_string(weights, "",
    "The pretrained weights to initialize finetuning. "
    "Cannot be set simultaneously with snapshot.");
DEFINE_int32(train_iterations, 0,
    "The number of iterations to run.");
//DEFINE_int32(numdata, 0,
//    "The total number of test data. (you should specify in this implementation)."); 
//DEFINE_int32(batchsize, 0,
//    "The batchsize. (you should specify in this implementation)."); 
//DEFINE_string(labellist, "",
//    "The text file having labels and their corresponding indices.");
//DEFINE_string(outfile, "",
//    "The text file including prediction probabilities.");


int main(int argc, char** argv) {
  // Print output to stderr (while still logging).
  FLAGS_alsologtostderr = 1;
  // Usage message.
  gflags::SetUsageMessage("\n"
      "usage: test_bn <args>\n\n");
  // Run tool or show usage.
  caffe::GlobalInit(&argc, &argv);
  if (argc == 2) {
//    return GetBrewFunction(caffe::string(argv[1]))();
  } else {
    gflags::ShowUsageWithFlagsRestrict(argv[0], "tools/test_bn");
  }

/*  // label (open label txt for label names)
  std::ifstream label_file;
  label_file.open(FLAGS_labellist.c_str());
  if(!label_file) {
    printf("Please specify the label list file. For example, ndsb_labels.txt.\n"); 
    return 0;  
  }
  std::vector< std::string > label_names;
  std::vector< int > label_indices;
  std::string label_name;
  int label_index; 
  int num_classes;
  while(label_file >> label_index >> label_name) {
//    printf("label_index: %d, label_name: %s\n", label_index, label_name.c_str()); 
    label_names.push_back(label_name); 
    label_indices.push_back(label_index); 
  }
  num_classes = label_indices.size(); 
//  printf("# of classes : %d\n", num_classes); 
*/
  //
  CHECK_GT(FLAGS_train_model.size(), 0) << "Need a train model definition to do preprosessing.";
  CHECK_GT(FLAGS_test_model.size(), 0) << "Need a test model definition to predict.";
  CHECK_GT(FLAGS_weights.size(), 0) << "Need model weights to predict.";

  // Set device id and mode
  if (FLAGS_gpu >= 0) {
    LOG(INFO) << "Use GPU with device ID " << FLAGS_gpu;
    Caffe::SetDevice(FLAGS_gpu);
    Caffe::set_mode(Caffe::GPU);
  } else {
    LOG(INFO) << "Use CPU.";
    Caffe::set_mode(Caffe::CPU);
  }
  // Instantiate the caffe net.
//  Caffe::set_phase(Caffe::TEST);
//  Net<float> caffe_net(FLAGS_model);
//  caffe_net.CopyTrainedLayersFrom(FLAGS_weights);
  Caffe::set_phase(Caffe::TRAIN);
  //Net<float> caffe_net(FLAGS_train_model);
  Net<float>* caffe_net_ptr = new Net<float>(FLAGS_train_model); 
  Net<float>& caffe_net = *caffe_net_ptr;
  caffe_net.CopyTrainedLayersFrom(FLAGS_weights);
  LOG(INFO) << FLAGS_weights;

  // Calculate iterations
  int iterations = -1, numdata = -1, batchsize = -1;
  const vector<shared_ptr<Layer<float> > >& layers = caffe_net.layers();
  //LOG(INFO) << "# of layers " <<  (int)layers.size();
  for (int i = 0; i < layers.size(); ++i) {
    const caffe::string& layername = layers[i]->layer_param().name();
    //LOG(INFO) << std::setfill(' ') << std::setw(10) << layername;
  }
  //LOG(INFO) << "layer type: " << layers[0]->layer_param().type(); 
  switch (layers[0]->layer_param().type()) {
    case 5: {// DATA
      batchsize = layers[0]->layer_param().data_param().batch_size();
      //LOG(INFO) << "batch_size: " << batch_size;
      int backend = (int)layers[0]->layer_param().data_param().backend();
      //LOG(INFO) << "backend (LEVELDB: 0, LMDB:1): " << backend;

      if (backend == 1) { // LMDB
        MDB_env* mdb_env;
        MDB_stat mdb_mst;
        CHECK_EQ(mdb_env_create(&mdb_env), MDB_SUCCESS) << "mdb_env_create failed";
        CHECK_EQ(mdb_env_set_mapsize(mdb_env, 1099511627776), MDB_SUCCESS);  // 1TB
        CHECK_EQ(mdb_env_open(mdb_env,
             layers[0]->layer_param().data_param().source().c_str(),
             MDB_RDONLY|MDB_NOTLS, 0664), MDB_SUCCESS) << "mdb_env_open failed";
        (void)mdb_env_stat(mdb_env, &mdb_mst);

        //LOG(INFO) << "FINALLY!!! # of images: " << mdb_mst.ms_entries;
        numdata = mdb_mst.ms_entries;
        
      } else { // LEVELDB
        //LOG(INFO) << "LEVELDB is currently not supported. sorry :)"; 
        return 0;
      }
      break;
    }
    case 12: { // IMAGE_DATA
      batchsize = layers[0]->layer_param().image_data_param().batch_size();
      //LOG(INFO) << "batch_size: " << batchsize;
      unsigned int number_of_lines = 0;       

      FILE *infile = fopen(layers[0]->layer_param().image_data_param().source().c_str(), "r");
      int ch;

      while (EOF != (ch=getc(infile)))
        if ('\n' == ch)
          ++number_of_lines;
      //printf("%u\n", number_of_lines);
      numdata = (int)number_of_lines; 
      break;
    }
    case 43: { // IMAGE_DATA_AFFINE
      batchsize = layers[0]->layer_param().image_data_affine_param().batch_size();
      //LOG(INFO) << "batch_size: " << batchsize;
      unsigned int number_of_lines = 0;

      FILE *infile = fopen(layers[0]->layer_param().image_data_affine_param().source().c_str(), "r");
      int ch;

      while (EOF != (ch=getc(infile)))
        if ('\n' == ch)
          ++number_of_lines;
      //printf("%u\n", number_of_lines);
      numdata = (int)number_of_lines;
      break;
    }
    case 44: { // IMAGE_DATA_MULTIPLE_INFERENCE
      batchsize = layers[0]->layer_param().image_data_multi_infer_param().batch_size();
      //LOG(INFO) << "batch_size: " << batchsize;
      unsigned int number_of_lines = 0;

      FILE *infile = fopen(layers[0]->layer_param().image_data_multi_infer_param().source().c_str(), "r");
      int ch;

      while (EOF != (ch=getc(infile)))
        if ('\n' == ch)
          ++number_of_lines;
      //printf("%u\n", number_of_lines);
      numdata = (int)number_of_lines;
      break;
    }
    default: 
      LOG(INFO) << "predict.cpp assumes layers[0] is either DATA or IMAGE_DATA.";
      return 0;
      break;
  }
  if (batchsize == -1 || numdata == -1) {
    //LOG(INFO) << "something wrong in reading # of data and batchsize.";
    return 0;
  } else {
    //LOG(INFO) << "num data: " << numdata << ", batchsize: " << batchsize;
  }
  if (FLAGS_train_iterations == 0) {
    iterations =(int)( (float)numdata / (float)batchsize ) + 1;
  } else {
    iterations = FLAGS_train_iterations; 
  }
  //LOG(INFO) << "# of iterations " << iterations; 

  // configure how many bn layers are in the network 
  //const vector<shared_ptr<Layer<float> > >& layers = caffe_net.layers();
  int num_bn_layers = 0; 
  vector<int> bn_layers; 
  bn_layers.resize(0); 
  for (int i = 0; i < layers.size(); ++i) {
    if (LayerParameter_LayerType_BN == layers[i]->layer_param().type()) {
      bn_layers.push_back(i); 
      //LOG(INFO) << std::setfill(' ') << std::setw(10) << layers[i]->layer_param().name() << " (" << bn_layers[num_bn_layers]+1 << " th layer)";
      num_bn_layers++;     
    }
  }
  // calculate mean (need to scannning every training data set)
  // for each iteration (of Forward())
  //   do summation of batch_mean_ (of BNLayer)
  //   do summation of E(X^2) (via batch_variance_)
  // 
  // calculate variance (need to scanning every training data set)
  // - E(X)^2 to buffer_
  // - E(X^2) - E(X)^2
  
  const vector<vector<Blob<float>*> >& bottom_vecs = caffe_net.bottom_vecs(); 
  int img_idx = 0, img_processed_idx = 0; 

  vector<shared_ptr<Blob<float> > > //spatial_mean_vecs,
                                    //spatial_variance_vecs, 
                                    batch_mean_vecs,
                                    batch_variance_vecs,
                                    //buffer_blob_vecs,
                                    //x_norm_vecs, 
                                    spatial_sum_multiplier_vecs, 
                                    batch_sum_multiplier_vecs;

//  spatial_mean_vecs.resize(num_bn_layers); 
//  spatial_variance_vecs.resize(num_bn_layers); 
  batch_mean_vecs.resize(num_bn_layers); 
  batch_variance_vecs.resize(num_bn_layers);
//  buffer_blob_vecs.resize(num_bn_layers); 
//  x_norm_vecs.resize(num_bn_layers);
  spatial_sum_multiplier_vecs.resize(num_bn_layers);
  batch_sum_multiplier_vecs.resize(num_bn_layers); 

  for (int k = 0; k < num_bn_layers; ++k) {
    
    const vector<Blob<float>*>& bottom = bottom_vecs[bn_layers[k]];

    // dimension
    int N = bottom[0]->num();
    int C = bottom[0]->channels();
    int H = bottom[0]->height();
    int W = bottom[0]->width();

    // fill spatial multiplier
    spatial_sum_multiplier_vecs[k].reset(new Blob<float>(1, 1, H, W)); 
    float* spatial_multiplier_data = spatial_sum_multiplier_vecs[k]->mutable_cpu_data();
    caffe_set(spatial_sum_multiplier_vecs[k]->count(), float(1), spatial_multiplier_data);
    // fill batch multiplier
    batch_sum_multiplier_vecs[k].reset(new Blob<float>(N, 1, 1, 1)); 
    float* batch_multiplier_data = batch_sum_multiplier_vecs[k]->mutable_cpu_data();
    caffe_set(batch_sum_multiplier_vecs[k]->count(), float(1), batch_multiplier_data);

    // x_norm
    //x_norm_vecs[k].reset(new Blob<float>(N, C, H, W));
    // mean
    //spatial_mean_vecs[k].reset(new Blob<float>(N, C, 1, 1));
    batch_mean_vecs[k].reset(new Blob<float>(1, C, 1, 1));
    float* batch_mean_data = batch_mean_vecs[k]->mutable_cpu_data(); 
    caffe_set(batch_mean_vecs[k]->count(), float(0), batch_mean_data); 

    // variance
    //spatial_variance_vecs[k].reset(new Blob<float>(N, C, 1, 1));
    batch_variance_vecs[k].reset(new Blob<float>(1, C, 1, 1));
    float* batch_variance_data = batch_variance_vecs[k]->mutable_cpu_data(); 
    caffe_set(batch_variance_vecs[k]->count(), float(0), batch_variance_data); 
    // buffer blob
    //buffer_blob_vecs[k].reset(new Blob<float>(N, C, H, W));
  }

  //LOG(INFO) << "Estimate batch norm and variance from training data for inference!"; 
  for (int i = 0; i < iterations; ++i) {
    //LOG(INFO) << "iter: " << i; 
    const vector<Blob<float>*>& result =
        caffe_net.ForwardPrefilled();
    //LOG(INFO) << "1111"; 
    // batch normalization for each BNLayer
    for (int k = 0; k < num_bn_layers; ++k) {
      //LOG(INFO) << "bn layer: " << k;
      const vector<Blob<float>*>& bottom = bottom_vecs[bn_layers[k]]; 
      //LOG(INFO) << "processing: " << layers[bn_layers[k]]->layer_param().name();
      // spatial mean & variance
      Blob<float> spatial_mean, spatial_variance;
      // batch mean & variance
      Blob<float> batch_mean, batch_variance;
      // buffer blob
      Blob<float> buffer_blob;
      // x_norm
      Blob<float> x_norm;

      // x_sum_multiplier is used to carry out sum using BLAS
      const shared_ptr<Blob<float> > spatial_sum_multiplier = spatial_sum_multiplier_vecs[k]; 
      const shared_ptr<Blob<float> > batch_sum_multiplier = batch_sum_multiplier_vecs[k];

      // dimension
      int N = bottom[0]->num();
      int C = bottom[0]->channels();
      int H = bottom[0]->height();
      int W = bottom[0]->width(); 

      // x_norm
      x_norm.Reshape(N, C, H, W);
      // mean
      spatial_mean.Reshape(N, C, 1, 1);
      batch_mean.Reshape(1, C, 1, 1);
      // variance
      spatial_variance.Reshape(N, C, 1, 1);
      batch_variance.Reshape(1, C, 1, 1);
      // buffer blod
      buffer_blob.Reshape(N, C, H, W);

      const float* const_bottom_data = bottom[0]->gpu_data();
      //LOG(INFO) << "2222"; 
      // put the squares of bottom into buffer_blob_
      caffe::caffe_gpu_powx(bottom[0]->count(), const_bottom_data, float(2),
          buffer_blob.mutable_gpu_data());

      // computes variance using var(X) = E(X^2) - (EX)^2
      // EX across spatial
      caffe::caffe_gpu_gemv<float>(CblasNoTrans, N * C, H * W, float(1. / (H * W)), const_bottom_data,
          spatial_sum_multiplier->gpu_data(), float(0), spatial_mean.mutable_gpu_data());
      // EX across batch
      caffe::caffe_gpu_gemv<float>(CblasTrans, N, C, float(1. / N), spatial_mean.gpu_data(),
          batch_sum_multiplier->gpu_data(), float(0), batch_mean.mutable_gpu_data());

      /******** update E[X] for whole data ***********/
      caffe::caffe_gpu_axpy<float>(C, float(1. / iterations), batch_mean.gpu_data(), batch_mean_vecs[k]->mutable_gpu_data()); 

      // E(X^2) across spatial
      caffe::caffe_gpu_gemv<float>(CblasNoTrans, N * C, H * W, float(1. / (H * W)), 
          buffer_blob.gpu_data(),
          spatial_sum_multiplier->gpu_data(), float(0), spatial_variance.mutable_gpu_data());
      // E(X^2) across batch
      caffe::caffe_gpu_gemv<float>(CblasTrans, N, C, float(1. / N), spatial_variance.gpu_data(),
          batch_sum_multiplier->gpu_data(), float(0), batch_variance.mutable_gpu_data());

      caffe::caffe_gpu_powx(batch_mean.count(), batch_mean.gpu_data(), float(2),
          buffer_blob.mutable_gpu_data());  // (EX)^2
      caffe::caffe_gpu_sub(batch_mean.count(), batch_variance.gpu_data(), buffer_blob.gpu_data(),
          batch_variance.mutable_gpu_data());  // variance 

      /******** update E[X] for whole data ***********/
      caffe::caffe_gpu_axpy<float>(C, float(1.  / (iterations - 1.)), batch_variance.gpu_data(), batch_variance_vecs[k]->mutable_gpu_data());
      //LOG(INFO) << "3333";
    }
    //LOG(INFO) << "4444"; 
    for (int j = 0; j < batchsize; ++j){
      if (img_idx < numdata) {

        // process each data

        ++img_processed_idx;
      }
      ++img_idx;
    }
    //LOG(INFO) << "5555"; 
    if (i % (int)(0.1*iterations) == 0) {
      //LOG(INFO) << float(i) / float(iterations) * 100 << "%"; 
    }
    //LOG(INFO) << "6666";
  }
  //LOG(INFO) << "100%"; 
  //LOG(INFO) << "# of imgs (read): " << img_idx << ", # of imgs (processed): " << img_processed_idx;


  /***************************** do prediction *****************************/
  // delete training net
  delete caffe_net_ptr; 
  // Instantiate the caffe net.
  Caffe::set_phase(Caffe::TEST);
  Net<float> caffe_test_net(FLAGS_test_model);
  //NetParameter param;
  //ReadNetParamsFromTextFileOrDie(FLAGS_test_model, &param);
  //caffe_net.Init(param);
  //caffe_net.CopyTrainedLayersFrom(FLAGS_weights);
  caffe_test_net.CopyTrainedLayersFrom(FLAGS_weights); 

  // Calculate iterations
  /*int*/ iterations = -1, numdata = -1, batchsize = -1;
  const vector<shared_ptr<Layer<float> > >& test_layers = caffe_test_net.layers();
  //LOG(INFO) << "# of layers " <<  (int)test_layers.size();
  for (int i = 0; i < test_layers.size(); ++i) {
    const caffe::string& layername = test_layers[i]->layer_param().name();
    //LOG(INFO) << std::setfill(' ') << std::setw(10) << layername;
  }
  //LOG(INFO) << "layer type: " << test_layers[0]->layer_param().type(); 
  switch (test_layers[0]->layer_param().type()) {
    case 5: {// DATA
      batchsize = test_layers[0]->layer_param().data_param().batch_size();
      //LOG(INFO) << "batch_size: " << batch_size;
      int backend = (int)test_layers[0]->layer_param().data_param().backend();
      //LOG(INFO) << "backend (LEVELDB: 0, LMDB:1): " << backend;

      if (backend == 1) { // LMDB
        MDB_env* mdb_env;
        MDB_stat mdb_mst;
        CHECK_EQ(mdb_env_create(&mdb_env), MDB_SUCCESS) << "mdb_env_create failed";
        CHECK_EQ(mdb_env_set_mapsize(mdb_env, 1099511627776), MDB_SUCCESS);  // 1TB
        CHECK_EQ(mdb_env_open(mdb_env,
             test_layers[0]->layer_param().data_param().source().c_str(),
             MDB_RDONLY|MDB_NOTLS, 0664), MDB_SUCCESS) << "mdb_env_open failed";
        (void)mdb_env_stat(mdb_env, &mdb_mst);

        //LOG(INFO) << "FINALLY!!! # of images: " << mdb_mst.ms_entries;
        numdata = mdb_mst.ms_entries;
        
      } else { // LEVELDB
        LOG(INFO) << "LEVELDB is currently not supported. sorry :)"; 
        return 0;
      }
      break;
    }
    case 12: { // IMAGE_DATA
      batchsize = test_layers[0]->layer_param().image_data_param().batch_size();
      //LOG(INFO) << "batch_size: " << batchsize;
      unsigned int number_of_lines = 0;       

      FILE *infile = fopen(test_layers[0]->layer_param().image_data_param().source().c_str(), "r");
      int ch;

      while (EOF != (ch=getc(infile)))
        if ('\n' == ch)
          ++number_of_lines;
      //printf("%u\n", number_of_lines);
      numdata = (int)number_of_lines; 
      break;
    }
    case 43: { // IMAGE_DATA_AFFINE
      batchsize = test_layers[0]->layer_param().image_data_affine_param().batch_size();
      //LOG(INFO) << "batch_size: " << batchsize;
      unsigned int number_of_lines = 0;

      FILE *infile = fopen(test_layers[0]->layer_param().image_data_affine_param().source().c_str(), "r");
      int ch;

      while (EOF != (ch=getc(infile)))
        if ('\n' == ch)
          ++number_of_lines;
      //printf("%u\n", number_of_lines);
      numdata = (int)number_of_lines;
      break;
    }
    case 44: { // IMAGE_DATA_MULTIPLE_INFERENCE
      batchsize = test_layers[0]->layer_param().image_data_multi_infer_param().batch_size();
      //LOG(INFO) << "batch_size: " << batchsize;
      unsigned int number_of_lines = 0;

      FILE *infile = fopen(test_layers[0]->layer_param().image_data_multi_infer_param().source().c_str(), "r");
      int ch;

      while (EOF != (ch=getc(infile)))
        if ('\n' == ch)
          ++number_of_lines;
      //printf("%u\n", number_of_lines);
      numdata = (int)number_of_lines;
      break;
    }  
    default: 
      LOG(INFO) << "predict.cpp assumes test_layers[0] is either DATA or IMAGE_DATA.";
      return 0;
      break;
  }
  if (batchsize == -1 || numdata == -1) {
    //LOG(INFO) << "something wrong in reading # of data and batchsize.";
    return 0;
  } else {
    //LOG(INFO) << "num data: " << numdata << ", batchsize: " << batchsize;
  }
  iterations =(int)( (float)numdata / (float)batchsize ) + 1;
  //LOG(INFO) << "# of iterations " << iterations; 
  //LOG(INFO) << "Running for " << FLAGS_iterations << " iterations.";

  int k_tmp = 0; 
  for (int i = 0; i < test_layers.size(); ++i) {
    if (LayerParameter_LayerType_BN == test_layers[i]->layer_param().type()) {
      bn_layers[k_tmp] = i; 
      //LOG(INFO) << std::setfill(' ') << std::setw(10) << test_layers[i]->layer_param().name() << " (" << bn_layers[k_tmp]+1 << " th layer)";
      k_tmp++;
    }
  }
  CHECK_EQ(k_tmp, num_bn_layers); 


  ////////////assigning batch mean and batch variance. 
  for (int k = 0; k < num_bn_layers; ++k) {
    const shared_ptr<BNLayer<float> > layer = 
        dynamic_pointer_cast<BNLayer<float> >(test_layers[bn_layers[k]]);  
    layer->set_batch_mean_and_batch_variance(
        *batch_mean_vecs[k].get(), *batch_variance_vecs[k].get());
    //Blob<float>& batch_mean_tmp = layer->batch_mean(); 
    //LOG(INFO) << "layer->batch_mean_vecs_.count(): " << batch_mean_tmp.count() 
    //          << ", batch_mean_vects" << batch_mean_vecs[k]->count();
  }

///////////////temp test
  vector<Blob<float>* > bottom_vec;
  vector<int> test_score_output_id;
  vector<float> test_score;
  float loss = 0;
  for (int i = 0; i < iterations; ++i) {
    float iter_loss;
    const vector<Blob<float>*>& result =
        caffe_test_net.Forward(bottom_vec, &iter_loss);
    loss += iter_loss;
    int idx = 0;
    for (int j = 0; j < result.size(); ++j) {
      const float* result_vec = result[j]->cpu_data();
      for (int k = 0; k < result[j]->count(); ++k, ++idx) {
        const float score = result_vec[k];
        if (i == 0) {
          test_score.push_back(score);
          test_score_output_id.push_back(j);
        } else {
          test_score[idx] += score;
        }
        const std::string& output_name = caffe_test_net.blob_names()[
            caffe_test_net.output_blob_indices()[j]];
        //LOG(INFO) << "Batch " << i << ", " << output_name << " = " << score;
      }
    }
  }
  loss /= iterations;
  LOG(INFO) << "Loss: " << loss;
  for (int i = 0; i < test_score.size(); ++i) {
    const std::string& output_name = caffe_test_net.blob_names()[
        caffe_test_net.output_blob_indices()[test_score_output_id[i]]];
    const float loss_weight =
        caffe_test_net.blob_loss_weights()[caffe_test_net.output_blob_indices()[i]];
    std::ostringstream loss_msg_stream;
    const float mean_score = test_score[i] / iterations;
    if (loss_weight) {
      loss_msg_stream << " (* " << loss_weight
                      << " = " << loss_weight * mean_score << " loss)";
    }
    LOG(INFO) << output_name << " = " << mean_score << loss_msg_stream.str();
  }

/*
  FILE *prediction_file;
  prediction_file = fopen(FLAGS_outfile.c_str(), "w"); 
  if (!prediction_file) {
    printf("Please specify the label list file. For example, prediction.txt.\n");
    return 0;  
  }
  
  //printf("# of iterations: %d\n", FLAGS_iterations);
  LOG(INFO) << "Start prediction"; 
  int img_idx = 0, img_processed_idx = 0;  
  for (int i = 0; i < iterations; ++i) {
//    printf("iter: %d\n", i); 
    const vector<Blob<float>*>& result =
        caffe_test_net.ForwardPrefilled();
//    printf("result.size(): %d\n", result.size()); 

    // data label (in here -1 for all data, since it is unlabeled data)
    int batchsize = result[0]->count(); 
//    printf("batchsize: %d, num_classes: %d\n", batchsize, num_classes); 
//    printf("result[0][0]: %.3f", result_vec[0]);
//    for (int j = 1; j < batchsize; ++j){
//      printf(", result[0][%d]: %.3f", result_vec[j]); 
//    } 
//    printf("\n"); 

    // prediction probs num_classes x batchsize 
    const float* prob_vec = result[1]->cpu_data(); 
    for (int j = 0; j < batchsize; ++j){
      if (img_idx < numdata) {
        fprintf(prediction_file, "%e", prob_vec[j*num_classes]); 
        for (int k = 1; k < num_classes; ++k) {
          fprintf(prediction_file, ",%e", prob_vec[j*num_classes+k]); 
        }
        fprintf(prediction_file, "\n"); 
        ++img_processed_idx;
      }
      ++img_idx;
    }    
    if (i % (int)(0.1*iterations) == 0) {
      LOG(INFO) << (float)i / (float)iterations * 100 << "%"; 
    }
  }
  LOG(INFO) << "100%"; 
  LOG(INFO) << "# of imgs (read): " << img_idx << ", # of imgs (processed): " << img_processed_idx;
  fclose(prediction_file); 
*/
  return 0; 
}
