/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
 */

#include <fstream>
#include <iostream>
using namespace std;

#ifndef _WIN32
#include <unistd.h>
#endif
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include "parse_regressor.h"
#include "loss_functions.h"
#include "global_data.h"
#include "io.h"
#include "rand48.h"

/* Define the last version where files are backward compatible. */
#define LAST_COMPATIBLE_VERSION "6.1.3"
#define VERSION_FILE_WITH_CUBIC "6.1.3"

void initialize_regressor(vw& all)
{
  size_t length = ((size_t)1) << all.num_bits;
  all.weight_mask = (all.stride * length) - 1;
  all.reg.weight_vectors = (weight *)calloc(all.stride*length, sizeof(weight));
  if (all.reg.weight_vectors == NULL)
    {
      cerr << all.program_name << ": Failed to allocate weight array with " << all.num_bits << " bits: try decreasing -b <bits>" << endl;
      exit (1);
    }
  
  if (all.per_feature_regularizer_input != "")
    {
      all.reg.regularizers = (weight *)calloc(2*length, sizeof(weight));
      if (all.reg.regularizers == NULL)
	{
	  cerr << all.program_name << ": Failed to allocate regularizers array: try decreasing -b <bits>" << endl;
	  exit (1);
	}
    }
  else
    all.reg.regularizers = NULL;

  // random weight initialization for matrix factorization
  if (all.random_weights)
    {
      if (all.rank > 0)
	for (size_t j = 0; j < all.stride*length; j++)
	  all.reg.weight_vectors[j] = (float) (0.1 * frand48()); 
      else
	for (size_t j = 0; j < length; j++)
	  all.reg.weight_vectors[j*all.stride] = (float)(frand48() - 0.5);
    }
  if (all.initial_weight != 0.)
    for (size_t j = 0; j < all.stride*length; j+=all.stride)
      all.reg.weight_vectors[j] = all.initial_weight;
  if (all.lda)
    {
      size_t stride = all.stride;
      
      for (size_t j = 0; j < stride*length; j+=stride)
	{
	  for (size_t k = 0; k < all.lda; k++) {
	    if (all.random_weights) {
	      all.reg.weight_vectors[j+k] = (float)(-log(frand48()) + 1.0f);
		  all.reg.weight_vectors[j+k] *= (float)(all.lda_D / all.lda / all.length() * 200);
	    }
	  }
	  all.reg.weight_vectors[j+all.lda] = all.initial_t;
	}
    }
  if(all.adaptive && all.initial_t > 0)
  {
    for (size_t j = 1; j < all.stride*length; j+=all.stride)
    {
      all.reg.weight_vectors[j] = all.initial_t;   //for adaptive update, we interpret initial_t as previously seeing initial_t fake datapoints, all with squared gradient=1
      //NOTE: this is not invariant to the scaling of the data (i.e. when combined with normalized). Since scaling the data scales the gradient, this should ideally be 
      //feature_range*initial_t, or something like that. We could potentially fix this by just adding this base quantity times the current range to the sum of gradients 
      //stored in memory at each update, and always start sum of gradients to 0, at the price of additional additions and multiplications during the update...
    }

  }
}

//nonreentrant
v_array<char> temp;

void read_vector(vw& all, const char* file, bool& initialized, bool reg_vector)
{
  ifstream source(file, ios::in | ios::binary);
  if (!source.is_open())
    {
      cout << "can't open " << file << endl << " ... exiting." << endl;
      exit(1);
    }
  
  size_t v_length;
  source.read((char*)&v_length, sizeof(v_length));
  temp.erase();
  if (temp.index() < v_length)
    reserve(temp, v_length);
  source.read(temp.begin,v_length);
  version_struct v_tmp(temp.begin);
  if (v_tmp < LAST_COMPATIBLE_VERSION)
    {
      cout << "Model has possibly incompatible version! " << v_tmp.to_string() << endl;
      exit(1);
    }
  
  source.read((char*)&all.sd->min_label, sizeof(all.sd->min_label));
  source.read((char*)&all.sd->max_label, sizeof(all.sd->max_label));
  
  size_t local_num_bits;
  source.read((char *)&local_num_bits, sizeof(local_num_bits));
  if (!initialized){
    if (all.default_bits != true && all.num_bits != local_num_bits)
      {
	cout << "Wrong number of bits for source!" << endl;
	exit (1);
      }
    all.default_bits = false;
    all.num_bits = local_num_bits;
  }
  else 
    if (local_num_bits != all.num_bits)
      {
	cout << "can't combine sources with different feature number!" << endl;
	exit (1);
      }
  
  int len;
  source.read((char *)&len, sizeof(len));
  
  vector<string> local_pairs;
  for (; len > 0; len--)
    {
      char pair[2];
      source.read(pair, sizeof(char)*2);
      string temp(pair, 2);
      local_pairs.push_back(temp);
    }

  vector<string> local_triples;
  if (v_tmp >= VERSION_FILE_WITH_CUBIC) {
    source.read((char *)&len, sizeof(len));
    for (; len > 0; len--)
      {
        char triple[3];
        source.read(triple, sizeof(char)*3);
        string temp(triple, 3);
        local_triples.push_back(temp);
      }
  }

  size_t local_rank;
  source.read((char*)&local_rank, sizeof(local_rank));
  size_t local_lda;
  source.read((char*)&local_lda, sizeof(local_lda));
  if (!initialized)
    {
      all.rank = local_rank;
      all.lda = local_lda;
      //initialized = true;
    }
  else
    {
      cout << "can't combine regressors" << endl;
      exit(1);
    }

  if (all.rank > 0)
    {
      float temp = ceilf(logf((float)(all.rank*2+1)) / logf (2.f));
      all.stride = 1 << (int) temp;
      all.random_weights = true;
    }
  
  if (all.lda > 0)
    {
      // par->sort_features = true;
      float temp = ceilf(logf((float)(all.lda*2+1)) / logf (2.f));
      all.stride = 1 << (int) temp;
      all.random_weights = false;
    }

  if (!initialized)
    {
      all.pairs = local_pairs;
      all.triples = local_triples;
      initialize_regressor(all);
    }
  else
    if (local_pairs != all.pairs)
      {
	cout << "can't combine sources with different features!" << endl;
	for (size_t i = 0; i < local_pairs.size(); i++)
	  cout << local_pairs[i] << " " << local_pairs[i].size() << " ";
	cout << endl;
	for (size_t i = 0; i < all.pairs.size(); i++)
	  cout << all.pairs[i] << " " << all.pairs[i].size() << " ";
	cout << endl;
	exit (1);
      }
  else
    if (local_triples != all.triples)
      {
	cout << "can't combine sources with different features!" << endl;
	for (size_t i = 0; i < local_triples.size(); i++)
	  cout << local_triples[i] << " " << local_triples[i].size() << " ";
	cout << endl;
	for (size_t i = 0; i < all.triples.size(); i++)
	  cout << all.triples[i] << " " << all.triples[i].size() << " ";
	cout << endl;
	exit (1);
      }
  size_t local_ngram;
  source.read((char*)&local_ngram, sizeof(local_ngram));
  size_t local_skips;
  source.read((char*)&local_skips, sizeof(local_skips));
  if (!initialized)
    {
      all.ngram = local_ngram;
      all.skips = local_skips;
      //initialized = true;
    }
  else
    if (all.ngram != local_ngram || all.skips != local_skips)
      {
	cout << "can't combine sources with different ngram features!" << endl;
	exit(1);
      }

  std::string local_options_from_file;
  size_t options_length;
  source.read((char*)&options_length, sizeof(options_length));
  temp.erase();
  if (temp.index() < options_length)
    reserve(temp, options_length);
  source.read(temp.begin,options_length);
  local_options_from_file.assign(temp.begin);
  if( !initialized ) {
    all.options_from_file.assign(local_options_from_file);
    initialized = true;
  }
  else if( all.options_from_file.compare(local_options_from_file) != 0 ) {
    cout << "can't combine sources created with different options!" << endl;
    exit(1);
  }

  size_t stride = all.stride;
  while (source.good())
    {
      uint32_t hash;
      source.read((char *)&hash, sizeof(hash));
      weight w = 0.;
      source.read((char *)&w, sizeof(float));
      
      if (source.good())
	{
	  if (all.rank != 0) 
	    all.reg.weight_vectors[hash] = w;
	  else 
	    if (all.lda == 0)
	      if (reg_vector) {
		all.reg.regularizers[hash] = w;
		if (hash%2 == 1) // This is the prior mean; previous element was prior variance
		  all.reg.weight_vectors[(hash/2*stride)] = w;
	      }
	      else 
		all.reg.weight_vectors[hash*stride] = w;
	    else 
	      all.reg.weight_vectors[hash] = w;
	}      
    }
  source.close();
}

void parse_regressor_args(vw& all, po::variables_map& vm, string& final_regressor_name, bool quiet)
{
  if (vm.count("final_regressor")) {
    final_regressor_name = vm["final_regressor"].as<string>();
    if (!quiet)
      cerr << "final_regressor = " << vm["final_regressor"].as<string>() << endl;
  }
  else
    final_regressor_name = "";

  vector<string> regs;
  if (vm.count("initial_regressor") || vm.count("i"))
    regs = vm["initial_regressor"].as< vector<string> >();
  
  /* 
     Read in regressors.  If multiple regressors are specified, do a weighted 
     average.  If none are specified, initialize according to global_seg & 
     numbits.
  */
  bool initialized = false;

  for (size_t i = 0; i < regs.size(); i++)
    read_vector(all, regs[i].c_str(), initialized, false);
  
  if (all.per_feature_regularizer_input != "")
    read_vector(all, all.per_feature_regularizer_input.c_str(), initialized, true);
      
  if (!initialized)
    {
      if(vm.count("noop") || vm.count("sendto"))
	{
	  all.reg.weight_vectors = NULL;
	  all.reg.regularizers = NULL;
	}
      else
	initialize_regressor(all);
    }
}

void free_regressor(regressor &r)
{
  if (r.weight_vectors != NULL)
    free(r.weight_vectors);
  if (r.regularizers != NULL)
    free(r.regularizers);
}

namespace {
void my_buf_write_file(io_buf& o, int f, const char* data, size_t len)
{
  char *p;

  buf_write (o, p, len);
  memcpy (p, data, len);
}
}

void dump_regressor(vw& all, string reg_name, bool as_text, bool reg_vector)
{
  if (reg_name == string(""))
    return;
  string start_name = reg_name+string(".writing");
  io_buf io_temp;

  int f = io_temp.open_file(start_name.c_str(),io_buf::WRITE);
  
  if (f<0)
    {
      cout << "can't open: " << start_name << " for writing, exiting" << endl;
      exit(1);
    }
  size_t v_length = version.to_string().length()+1;
  if (!as_text) {
    my_buf_write_file(io_temp,f,(char*)&v_length, sizeof(v_length));
    my_buf_write_file(io_temp,f,version.to_string().c_str(),v_length);
  
    my_buf_write_file(io_temp,f,(char*)&all.sd->min_label, sizeof(all.sd->min_label));
    my_buf_write_file(io_temp,f,(char*)&all.sd->max_label, sizeof(all.sd->max_label));
  
    my_buf_write_file(io_temp,f,(char *)&all.num_bits, sizeof(all.num_bits));
    int len = all.pairs.size();
    my_buf_write_file(io_temp,f,(char *)&len, sizeof(len));
    for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
      my_buf_write_file(io_temp,f,i->c_str(),2);

    len = all.triples.size();
    my_buf_write_file(io_temp,f,(char *)&len, sizeof(len));
    for (vector<string>::iterator i = all.triples.begin(); i != all.triples.end();i++) 
      my_buf_write_file(io_temp,f,i->c_str(),3);

    my_buf_write_file(io_temp,f,(char*)&all.rank, sizeof(all.rank));
    my_buf_write_file(io_temp,f,(char*)&all.lda, sizeof(all.lda));

    my_buf_write_file(io_temp,f,(char*)&all.ngram, sizeof(all.ngram));
    my_buf_write_file(io_temp,f,(char*)&all.skips, sizeof(all.skips));


    size_t options_length = all.options_from_file.length()+1;
    my_buf_write_file(io_temp,f,(char*)&options_length, sizeof(options_length));
    my_buf_write_file(io_temp,f,all.options_from_file.c_str(), options_length);
  }
  else {
    char buff[512];
    int len;
    len = sprintf(buff, "Version %s\n", version.to_string().c_str());
    my_buf_write_file(io_temp,f, buff, len);
    len = sprintf(buff, "Min label:%f max label:%f\n", all.sd->min_label, all.sd->max_label);
    my_buf_write_file(io_temp,f, buff, len);
    len = sprintf(buff, "bits:%d\n", (int)all.num_bits);
    my_buf_write_file(io_temp,f, buff, len);
    for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) {
      len = sprintf(buff, "%s ", i->c_str());
      my_buf_write_file(io_temp,f, buff, len);
    }
    if (all.pairs.size() > 0)
      {
	len = sprintf(buff, "\n");
	my_buf_write_file(io_temp,f, buff, len);
      }
    for (vector<string>::iterator i = all.triples.begin(); i != all.triples.end();i++) {
      len = sprintf(buff, "%s ", i->c_str());
      io_temp.write_file(f, buff, len);
    }
    if (all.triples.size() > 0)
      {
	len = sprintf(buff, "\n");
	io_temp.write_file(f, buff, len);
      }
    len = sprintf(buff, "ngram:%d skips:%d\nindex:weight pairs:\n", (int)all.ngram, (int)all.skips);
    my_buf_write_file(io_temp,f, buff, len);

    len = sprintf(buff, "rank:%d\n", (int)all.rank);
    my_buf_write_file(io_temp,f, buff, len);

    len = sprintf(buff, "lda:%d\n", (int)all.lda);
    my_buf_write_file(io_temp,f, buff, len);

    len = sprintf(buff, "options:%s\n", all.options_from_file.c_str());
    my_buf_write_file(io_temp,f, buff, len);
  }
  
  uint32_t length = 1 << all.num_bits;
  size_t stride = all.stride;
  if (reg_vector)
    length *= 2;
  for(uint32_t i = 0; i < length; i++)
    {
      if ((all.lda == 0) && (all.rank == 0))
	{
	  weight v;
	  if (reg_vector)
	    v = all.reg.regularizers[i];
	  else
	    v = all.reg.weight_vectors[stride*i];
	  if (v != 0.)
	    {
              if (!as_text) {
                my_buf_write_file(io_temp,f,(char *)&i, sizeof (i));
                my_buf_write_file(io_temp,f,(char *)&v, sizeof (v));
              } else {
                char buff[512];
                int len = sprintf(buff, "%d:%f\n", i, v);
                my_buf_write_file(io_temp,f, buff, len);
              }
	    }
	}
      else
	{
	  size_t K = all.lda;

	  if (all.rank != 0)
	    K = all.rank*2+1;
	  
          for (size_t k = 0; k < K; k++)
            {
              weight v = all.reg.weight_vectors[stride*i+k];
              uint32_t ndx = stride*i+k;
              if (!as_text) {
                my_buf_write_file(io_temp,f,(char *)&ndx, sizeof (ndx));
                my_buf_write_file(io_temp,f,(char *)&v, sizeof (v));
              } else {
                char buff[512];
		int len;

		if (all.rank != 0)
		  len = sprintf(buff, "%f ", v);
		else
		  len = sprintf(buff, "%f ", v + all.lda_rho);

                my_buf_write_file(io_temp,f, buff, len);
              }
            }
          if (as_text)
            my_buf_write_file(io_temp,f, "\n", 1);
	}
    }

  io_temp.flush(); // close_file() should do this for me ...
  io_temp.close_file();
  remove(reg_name.c_str());
  rename(start_name.c_str(),reg_name.c_str());
}

void save_predictor(vw& all, string reg_name, size_t current_pass)
{
  char* filename = new char[reg_name.length()+4];
  if (all.save_per_pass)
    sprintf(filename,"%s.%lu",reg_name.c_str(),(long unsigned)current_pass);
  else
    sprintf(filename,"%s",reg_name.c_str());
  dump_regressor(all, string(filename), false, false);
  delete[] filename;
}

void finalize_regressor(vw& all, string reg_name)
{
  dump_regressor(all, reg_name, false, false);
  dump_regressor(all, all.text_regressor_name, true, false);
  dump_regressor(all, all.per_feature_regularizer_output, false, true);
  dump_regressor(all, all.per_feature_regularizer_text, true, true);
  free_regressor(all.reg);
}
