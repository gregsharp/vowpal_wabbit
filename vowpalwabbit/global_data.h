/*
Copyright (c) 2009 Yahoo! Inc.  All rights reserved.  The copyrights
embodied in the content of this file are licensed under the BSD
(revised) open source license
 */

#ifndef GLOBAL_DATA_H
#define GLOBAL_DATA_H
#include <vector>
#include <stdint.h>
#include <cstdio>
#include "v_array.h"
#include "parse_primitives.h"
#include "loss_functions.h"
#include "comp_io.h"
#include "example.h"
#include "config.h"

struct version_struct {
  int major;
  int minor;
  int rev;
  version_struct(int maj, int min, int rv)
  {
    major = maj;
    minor = min;
    rev = rv;
  }
  version_struct(const char* v_str)
  {
    from_string(v_str);
  }
  void operator=(version_struct v){
    major = v.major;
    minor = v.minor;
    rev = v.rev;
  }
  void operator=(const char* v_str){
    from_string(v_str);
  }
  bool operator==(version_struct v){
    return (major == v.major && minor == v.minor && rev == v.rev);
  }
  bool operator==(const char* v_str){
    version_struct v_tmp(v_str);
    return (*this == v_tmp);
  }
  bool operator!=(version_struct v){
    return !(*this == v);
  }
  bool operator!=(const char* v_str){
    version_struct v_tmp(v_str);
    return (*this != v_tmp);
  }
  bool operator>=(version_struct v){
    if(major < v.major) return false;
    if(major > v.major) return true;
    if(minor < v.minor) return false;
    if(minor > v.minor) return true;
    if(rev >= v.rev ) return true;
    return false;
  }
  bool operator>=(const char* v_str){
    version_struct v_tmp(v_str);
    return (*this >= v_tmp);
  }
  bool operator>(version_struct v){
    if(major < v.major) return false;
    if(major > v.major) return true;
    if(minor < v.minor) return false;
    if(minor > v.minor) return true;
    if(rev > v.rev ) return true;
    return false;
  }
  bool operator>(const char* v_str){
    version_struct v_tmp(v_str);
    return (*this > v_tmp);
  }
  bool operator<=(version_struct v){
    return !(*this < v);
  }
  bool operator<=(const char* v_str){
    version_struct v_tmp(v_str);
    return (*this <= v_tmp);
  }
  bool operator<(version_struct v){
    return !(*this >= v);
  }
  bool operator<(const char* v_str){
    version_struct v_tmp(v_str);
    return (*this < v_tmp);
  }
  std::string to_string() const
  {
    char v_str[32];
    std::sprintf(v_str,"%d.%d.%d",major,minor,rev);
    std::string s = v_str;
    return s;
  }
  void from_string(const char* str)
  {
    std::sscanf(str,"%d.%d.%d",&major,&minor,&rev);
  }
};

const version_struct version(PACKAGE_VERSION);

typedef float weight;

struct regressor {
  weight* weight_vectors;
  weight* regularizers;
};

struct vw {
  shared_data* sd;

  parser* p;

  void (*driver)(void *);
  void (*learn)(void *, example*);
  void (*finish)(void *);
  void (*set_minmax)(shared_data* sd, double label);

  size_t num_bits; // log_2 of the number of features.
  bool default_bits;

  string data_filename; // was vm["data"]

  bool daemon; 
  size_t num_children;

  bool save_per_pass;
  float active_c0;
  float initial_weight;

  bool bfgs;
  bool hessian_on;
  int m;

  bool sequence;
  bool searn;
  size_t searn_nb_actions;
  std::string searn_base_learner;
  size_t searn_trained_nb_policies;
  size_t searn_total_nb_policies;
  float searn_beta;
  std::string searn_task;
  size_t searn_sequencetask_history;
  size_t searn_sequencetask_features;
  bool searn_sequencetask_bigrams;
  bool searn_sequencetask_bigram_features;

  size_t stride;

  std::string per_feature_regularizer_input;
  std::string per_feature_regularizer_output;
  std::string per_feature_regularizer_text;
  
  float l1_lambda; //the level of l_1 regularization to impose.
  float l2_lambda; //the level of l_2 regularization to impose.
  float power_t;//the power on learning rate decay.
  int reg_mode;

  size_t minibatch;

  float rel_threshold; // termination threshold

  size_t pass_length;
  size_t numpasses;
  size_t passes_complete;
  size_t parse_mask; // 1 << num_bits -1
  size_t weight_mask; // (stride*(1 << num_bits) -1)
  std::vector<std::string> pairs; // pairs of features to cross.
  bool ignore_some;
  bool ignore[256];//a set of namespaces to ignore
  size_t ngram;//ngrams to generate.
  size_t skips;//skips in ngrams.
  bool audit;//should I print lots of debugging information?
  bool quiet;//Should I suppress updates?
  bool training;//Should I train if label data is available?
  bool active;
  bool active_simulation;
  bool adaptive;//Should I use adaptive individual learning rates?
  bool exact_adaptive_norm;//Should I use the exact norm when computing the update?
  bool random_weights;
  bool add_constant;
  bool nonormalize;

  size_t lda;
  float lda_alpha;
  float lda_rho;
  float lda_D;

  std::string text_regressor_name;
  
  std::string span_server;

  size_t length () { return 1 << num_bits; };

  size_t rank;

  //Prediction output
  v_array<size_t> final_prediction_sink; // set to send global predictions to.
  int raw_prediction; // file descriptors for text output.
  size_t unique_id; //unique id for each node in the network, id == 0 means extra io.
  size_t total; //total number of nodes
  size_t node; //node id number

  void (*print)(int,float,float,v_array<char>);
  void (*print_text)(int, string, v_array<char>);
  loss_function* loss;

  char* program_name;

  //runtime accounting variables. 
  double initial_t;
  float eta;//learning rate control.
  float eta_decay_rate;

  std::string final_regressor_name;
  regressor reg;

  vw();
};

void print_result(int f, float res, float weight, v_array<char> tag);
void binary_print_result(int f, float res, float weight, v_array<char> tag);
void noop_mm(shared_data*, double label);
void print_lda_result(vw& all, int f, float* res, float weight, v_array<char> tag);
void get_prediction(int sock, float& res, float& weight);

#endif
