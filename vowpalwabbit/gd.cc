/*
Copyright (c) 2009 Yahoo! Inc.  All rights reserved.  The copyrights
embodied in the content of this file are licensed under the BSD
(revised) open source license
 */
#include <fstream>
#include <sstream>
#include <float.h>
#include <netdb.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>

#if defined(__SSE2__) && !defined(VW_LDA_NO_SSE)
#include <xmmintrin.h>
#endif

#include "parse_example.h"
#include "constant.h"
#include "sparse_dense.h"
#include "gd.h"
#include "cache.h"
#include "simple_label.h"
#include "allreduce.h"
#include "accumulate.h"

using namespace std;

void adaptive_inline_train(vw& all, example* &ec, float update);
void inline_train(vw& all, example* &ec, float update);
void general_adaptive_train(vw&, example* &ec, float update, float power_t);

//nonreentrant
size_t gd_current_pass = 0;

void predict(vw& all, example* ex);
void sync_weights(vw& all);

void learn_gd(void* a, example* ec)
{
  vw* all = (vw*)a;
  assert(ec->in_use);
  if (ec->pass != gd_current_pass)
    {
      if(all->span_server != "") {
	if(all->adaptive)
	  accumulate_weighted_avg(*all, all->span_server, all->reg);
	else 
	  accumulate_avg(*all, all->span_server, all->reg, 0);	      
      }
      
      if (all->save_per_pass)
	sync_weights(*all);
      all->eta *= all->eta_decay_rate;
      save_predictor(*all, all->final_regressor_name, gd_current_pass);
      gd_current_pass = ec->pass;
    }
  
  if (!command_example(*all, ec))
    {
      predict(*all,ec);
      if (ec->eta_round != 0.)
	{
	  if (all->adaptive)
	    if (all->power_t == 0.5 || !all->exact_adaptive_norm)
	      adaptive_inline_train(*all,ec,ec->eta_round);
	    else
	      general_adaptive_train(*all,ec,ec->eta_round,all->power_t);
	  else
	    inline_train(*all, ec, ec->eta_round);
	  if (all->sd->contraction < 1e-10)  // updating weights now to avoid numerical instability
	    sync_weights(*all);
	  
	}
    }
}

void finish_gd(void* a)
{
  vw* all = (vw*)a;
  sync_weights(*all);
  if(all->span_server != "") {
    if(all->adaptive)
      accumulate_weighted_avg(*all, all->span_server, all->reg);
    else 
      accumulate_avg(*all, all->span_server, all->reg, 0);	      
  }
}

void sync_weights(vw& all) {
  if (all.sd->gravity == 0. && all.sd->contraction == 1.)  // to avoid unnecessary weight synchronization
    return;
  uint32_t length = 1 << all.num_bits;
  size_t stride = all.stride;
  for(uint32_t i = 0; i < length && all.reg_mode; i++)
    all.reg.weight_vectors[stride*i] = trunc_weight(all.reg.weight_vectors[stride*i], all.sd->gravity) * all.sd->contraction;
  all.sd->gravity = 0.;
  all.sd->contraction = 1.;
}

bool command_example(vw& all, example* ec) {
  if (ec->indices.index() > 1)
    return false;

  if (ec->tag.index() >= 4 && !strncmp((const char*) ec->tag.begin, "save", 4))
    {//save state
      string final_regressor_name = all.final_regressor_name;

      if ((ec->tag).index() >= 6 && (ec->tag)[4] == '_')
	final_regressor_name = string(ec->tag.begin+5, (ec->tag).index()-5);

      if (!all.quiet)
	cerr << "saving regressor to " << final_regressor_name << endl;
      save_predictor(all, final_regressor_name, 0);

      return true;
    }
  return false;
}

float finalize_prediction(vw& all, float ret) 
{
  if ( isnan(ret))
    {
      cout << "you have a NAN!!!!!" << endl;
      return 0.;
    }
  if ( ret > all.sd->max_label )
    return all.sd->max_label;
  if (ret < all.sd->min_label)
    return all.sd->min_label;

  return ret;
}

void finish_example(vw& all, example* ec)
{
  return_simple_example(all, ec);
}

float inline_predict_trunc(vw& all, example* &ec)
{
  float prediction = all.p->lp->get_initial(ec->ld);
  
  weight* weights = all.reg.weight_vectors;
  size_t mask = all.weight_mask;
  for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
    prediction += sd_add_trunc(weights,mask,ec->atomics[*i].begin, ec->atomics[*i].end, all.sd->gravity);
  
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    {
      if (ec->atomics[(int)(*i)[0]].index() > 0)
	{
	  v_array<feature> temp = ec->atomics[(int)(*i)[0]];
	  for (; temp.begin != temp.end; temp.begin++)
	    prediction += one_pf_quad_predict_trunc(weights, *temp.begin,
						    ec->atomics[(int)(*i)[1]], mask, all.sd->gravity);
	}
    }
  
  return prediction;
}

float inline_predict(vw& all, example* &ec)
{
  float prediction = all.p->lp->get_initial(ec->ld);

  weight* weights = all.reg.weight_vectors;
  size_t mask = all.weight_mask;
  for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
    prediction += sd_add(weights,mask,ec->atomics[*i].begin, ec->atomics[*i].end);
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    {
      if (ec->atomics[(int)(*i)[0]].index() > 0)
	{
	  v_array<feature> temp = ec->atomics[(int)(*i)[0]];
	  for (; temp.begin != temp.end; temp.begin++)
	    prediction += one_pf_quad_predict(weights,*temp.begin,
					      ec->atomics[(int)(*i)[1]],mask);
	}
    }
  
  return prediction;
}

struct string_value {
  float v;
  string s;
  friend bool operator<(const string_value& first, const string_value& second);
};

bool operator<(const string_value& first, const string_value& second)
{
  return fabs(first.v) > fabs(second.v);
}

#include <algorithm>

void print_audit_quad(vw& all, weight* weights, audit_data& page_feature, v_array<audit_data> &offer_features, vector<string_value>& features)
{
  size_t halfhash = quadratic_constant * page_feature.weight_index;

  for (audit_data* ele = offer_features.begin; ele != offer_features.end; ele++)
    {
      ostringstream tempstream;
      tempstream << page_feature.space << '^' << page_feature.feature << '^' 
		 << ele->space << '^' << ele->feature << ':' << (((halfhash + ele->weight_index)/all.stride) & all.parse_mask)
		 << ':' << ele->x*page_feature.x
		 << ':' << trunc_weight(weights[(halfhash + ele->weight_index) & all.weight_mask], all.sd->gravity) * all.sd->contraction;
      string_value sv = {weights[ele->weight_index & all.weight_mask]*ele->x, tempstream.str()};
      features.push_back(sv);
    }
}

void print_quad(vw& all, weight* weights, feature& page_feature, v_array<feature> &offer_features, vector<string_value>& features)
{
  size_t halfhash = quadratic_constant * page_feature.weight_index;
  for (feature* ele = offer_features.begin; ele != offer_features.end; ele++)
    {
      ostringstream tempstream;
      tempstream << (((halfhash + ele->weight_index)/all.stride) & all.parse_mask) 
		 << ':' << (ele->x*page_feature.x)
		 << ':' << trunc_weight(weights[(halfhash + ele->weight_index) & all.weight_mask], all.sd->gravity) * all.sd->contraction;
      string_value sv = {weights[ele->weight_index & all.weight_mask]*ele->x, tempstream.str()};
      features.push_back(sv);
    }
}

void print_features(vw& all, example* &ec)
{
  weight* weights = all.reg.weight_vectors;
  size_t stride = all.stride;

  if (all.lda > 0)
    {
      size_t count = 0;
      for (size_t* i = ec->indices.begin; i != ec->indices.end; i++)
	count += ec->audit_features[*i].index() + ec->atomics[*i].index();
      for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
	for (audit_data *f = ec->audit_features[*i].begin; f != ec->audit_features[*i].end; f++)
	  {
	    cout << '\t' << f->space << '^' << f->feature << ':' << (f->weight_index/all.stride & all.parse_mask) << ':' << f->x;
	    for (size_t k = 0; k < all.lda; k++)
	      cout << ':' << weights[(f->weight_index+k) & all.weight_mask];
	  }
      cout << " total of " << count << " features." << endl;
    }
  else
    {
      vector<string_value> features;

      for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
	if (ec->audit_features[*i].begin != ec->audit_features[*i].end)
	  for (audit_data *f = ec->audit_features[*i].begin; f != ec->audit_features[*i].end; f++)
	    {
	      ostringstream tempstream;
	      tempstream << f->space << '^' << f->feature << ':' << (f->weight_index/stride & all.parse_mask) << ':' << f->x;
	      tempstream  << ':' << trunc_weight(weights[f->weight_index & all.weight_mask], all.sd->gravity) * all.sd->contraction;
	      if(all.adaptive)
		tempstream << '@' << weights[(f->weight_index+1) & all.weight_mask];
	      string_value sv = {weights[f->weight_index & all.weight_mask]*f->x, tempstream.str()};
	      features.push_back(sv);
	    }
	else
	  for (feature *f = ec->atomics[*i].begin; f != ec->atomics[*i].end; f++)
	    {
	      ostringstream tempstream;
	      if ( f->weight_index == ((constant*stride)&all.weight_mask))
		tempstream << "Constant:";
	      tempstream << (f->weight_index/stride & all.parse_mask) << ':' << f->x;
	      tempstream << ':' << trunc_weight(weights[f->weight_index & all.weight_mask], all.sd->gravity) * all.sd->contraction;
	      if(all.adaptive)
		tempstream << '@' << weights[(f->weight_index+1) & all.weight_mask];
	      string_value sv = {weights[f->weight_index & all.weight_mask]*f->x, tempstream.str()};
	      features.push_back(sv);
	    }
      for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
	if (ec->audit_features[(int)(*i)[0]].begin != ec->audit_features[(int)(*i)[0]].end)
	  for (audit_data* f = ec->audit_features[(int)(*i)[0]].begin; f != ec->audit_features[(int)(*i)[0]].end; f++)
	    print_audit_quad(all, weights, *f, ec->audit_features[(int)(*i)[1]], features);
	else
	  for (feature* f = ec->atomics[(int)(*i)[0]].begin; f != ec->atomics[(int)(*i)[0]].end; f++)
	    print_quad(all, weights, *f, ec->atomics[(int)(*i)[1]], features);      

      sort(features.begin(),features.end());

      for (vector<string_value>::iterator sv = features.begin(); sv!= features.end(); sv++)
	cout << '\t' << (*sv).s;
      cout << endl;
    }
}

void print_audit_features(vw& all, example* ec)
{
  fflush(stdout);
  print_result(fileno(stdout),ec->final_prediction,-1,ec->tag);
  fflush(stdout);
  print_features(all, ec);
}

void one_pf_quad_update(weight* weights, feature& page_feature, v_array<feature> &offer_features, size_t mask, float update)
{
  size_t halfhash = quadratic_constant * page_feature.weight_index;
  update *= page_feature.x;
  for (feature* ele = offer_features.begin; ele != offer_features.end; ele++)
    weights[(halfhash + ele->weight_index) & mask] += update * ele->x;
}

float InvSqrt(float x){
  float xhalf = 0.5f * x;
  int i = *(int*)&x; // store floating-point bits in integer
  i = 0x5f3759d5 - (i >> 1); // initial guess for Newton's method
  x = *(float*)&i; // convert new bits into float
  x = x*(1.5f - xhalf*x*x); // One round of Newton's method
  return x;
}

void one_pf_quad_adaptive_update(weight* weights, feature& page_feature, v_array<feature> &offer_features, size_t mask, float update, float g, example* ec)
{
  size_t halfhash = quadratic_constant * page_feature.weight_index;
  update *= page_feature.x;
  float update2 = g * page_feature.x * page_feature.x;

  for (feature* ele = offer_features.begin; ele != offer_features.end; ele++)
    {
      weight* w=&weights[(halfhash + ele->weight_index) & mask];
      w[1] += update2 * ele->x * ele->x;
#if defined(__SSE2__) && !defined(VW_LDA_NO_SSE)
      float t;
      __m128 eta = _mm_load_ss(&w[1]);
      eta = _mm_rsqrt_ss(eta);
      _mm_store_ss(&t, eta);
      t *= ele->x;
#else
      float t = ele->x*InvSqrt(w[1]);
#endif
      w[0] += update * t;
    }
}

void offset_quad_update(weight* weights, feature& page_feature, v_array<feature> &offer_features, size_t mask, float update, size_t offset)
{
  size_t halfhash = quadratic_constant * page_feature.weight_index + offset;
  update *= page_feature.x;
  for (feature* ele = offer_features.begin; ele != offer_features.end; ele++)
    weights[(halfhash + ele->weight_index) & mask] += update * ele->x;
}

void adaptive_inline_train(vw& all, example* &ec, float update)
{
  if (fabs(update) == 0.)
    return;

  size_t mask = all.weight_mask;
  label_data* ld = (label_data*)ec->ld;
  weight* weights = all.reg.weight_vectors;
  
  float g = all.loss->getSquareGrad(ec->final_prediction, ld->label) * ld->weight;
  for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
    {
      feature *f = ec->atomics[*i].begin;
      for (; f != ec->atomics[*i].end; f++)
	{
	  weight* w = &weights[f->weight_index & mask];
	  w[1] += g * f->x * f->x;
#if defined(__SSE2__) && !defined(VW_LDA_NO_SSE)
      float t;
      __m128 eta = _mm_load_ss(&w[1]);
      eta = _mm_rsqrt_ss(eta);
      _mm_store_ss(&t, eta);
      t *= f->x;
#else
	  float t = f->x*InvSqrt(w[1]);
#endif
	  w[0] += update * t;
	}
    }
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    {
      if (ec->atomics[(int)(*i)[0]].index() > 0)
	{
	  v_array<feature> temp = ec->atomics[(int)(*i)[0]];
	  for (; temp.begin != temp.end; temp.begin++)
	    one_pf_quad_adaptive_update(weights, *temp.begin, ec->atomics[(int)(*i)[1]], mask, update, g, ec);
	} 
    }
}

void quad_general_adaptive_update(weight* weights, feature& page_feature, v_array<feature> &offer_features, size_t mask, float update, float g, example* ec, float power_t)
{
  size_t halfhash = quadratic_constant * page_feature.weight_index;
  update *= page_feature.x;
  float update2 = g * page_feature.x * page_feature.x;
  
  for (feature* ele = offer_features.begin; ele != offer_features.end; ele++)
    {
      weight* w=&weights[(halfhash + ele->weight_index) & mask];
      w[1] += update2 * ele->x * ele->x;
      float t = ele->x*powf(w[1],-power_t);
      w[0] += update * t;
    }
}

void general_adaptive_train(vw& all, example* &ec, float update, float power_t)
{
  if (fabs(update) == 0.)
    return;
  
  size_t mask = all.weight_mask;
  label_data* ld = (label_data*)ec->ld;
  weight* weights = all.reg.weight_vectors;
  
  float g = all.loss->getSquareGrad(ec->final_prediction, ld->label) * ld->weight;
  for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
    {
      feature *f = ec->atomics[*i].begin;
      for (; f != ec->atomics[*i].end; f++)
	{
	  weight* w = &weights[f->weight_index & mask];
	  w[1] += g * f->x * f->x;
	  float t = f->x*powf(w[1],-power_t);
	  w[0] += update * t;
	}
    }
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    {
      if (ec->atomics[(int)(*i)[0]].index() > 0)
	{
	  v_array<feature> temp = ec->atomics[(int)(*i)[0]];
	  for (; temp.begin != temp.end; temp.begin++)
	    quad_general_adaptive_update(weights, *temp.begin, ec->atomics[(int)(*i)[1]], mask, update, g, ec, power_t);
	} 
    }
}


float xGx_quad(weight* weights, feature& page_feature, v_array<feature> &offer_features, size_t mask, float g)
{
  size_t halfhash = quadratic_constant * page_feature.weight_index;
  float xGx = 0.;
  float update2 = g * page_feature.x * page_feature.x;
  for (feature* ele = offer_features.begin; ele != offer_features.end; ele++)
    {
      weight* w=&weights[(halfhash + ele->weight_index) & mask];
#if defined(__SSE2__) && !defined(VW_LDA_NO_SSE)
      float m = w[1] + update2 * ele->x * ele->x;
      __m128 eta = _mm_load_ss(&m);
      eta = _mm_rsqrt_ss(eta);
      _mm_store_ss(&m, eta);
      float t = ele->x * m;
#else
      float t = ele->x*InvSqrt(w[1] + update2 * ele->x * ele->x);
#endif
      xGx += t * ele->x;
    }
  return xGx;
}

float xGx_general_quad(weight* weights, feature& page_feature, v_array<feature> &offer_features, size_t mask, float g, float power_t)
{
  size_t halfhash = quadratic_constant * page_feature.weight_index;
  float xGx = 0.;
  float update2 = g * page_feature.x * page_feature.x;
  for (feature* ele = offer_features.begin; ele != offer_features.end; ele++)
    {
      weight* w=&weights[(halfhash + ele->weight_index) & mask];
      float t = ele->x*powf(w[1] + update2 * ele->x * ele->x,- power_t);
      xGx += t * ele->x;
    }
  return xGx;
}

float compute_general_xGx(vw& all, example* &ec, float power_t)
{//We must traverse the features in _precisely_ the same order as during training.
  size_t mask = all.weight_mask;
  label_data* ld = (label_data*)ec->ld;
  float g = all.loss->getSquareGrad(ec->final_prediction, ld->label) * ld->weight;
  if (g==0) return 1.;

  float xGx = 0.;
  
  weight* weights = all.reg.weight_vectors;
  for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
    {
      feature *f = ec->atomics[*i].begin;
      for (; f != ec->atomics[*i].end; f++)
	{
	  weight* w = &weights[f->weight_index & mask];
	  float t = f->x*powf(w[1] + g * f->x * f->x,- power_t);
	  xGx += t * f->x;
	}
    }
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    {
      if (ec->atomics[(int)(*i)[0]].index() > 0)
	{
	  v_array<feature> temp = ec->atomics[(int)(*i)[0]];
	  for (; temp.begin != temp.end; temp.begin++)
	    xGx += xGx_general_quad(weights, *temp.begin, ec->atomics[(int)(*i)[1]], mask, g, power_t);
	} 
    }
  
  return xGx;
}

float compute_xGx(vw& all, example* &ec)
{//We must traverse the features in _precisely_ the same order as during training.
  size_t mask = all.weight_mask;
  label_data* ld = (label_data*)ec->ld;
  float g = all.loss->getSquareGrad(ec->final_prediction, ld->label) * ld->weight;
  if (g==0) return 1.;

  float xGx = 0.;
  
  weight* weights = all.reg.weight_vectors;
  for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
    {
      feature *f = ec->atomics[*i].begin;
      for (; f != ec->atomics[*i].end; f++)
	{
	  weight* w = &weights[f->weight_index & mask];
#if defined(__SSE2__) && !defined(VW_LDA_NO_SSE)
      float m = w[1] + g * f->x * f->x;
      __m128 eta = _mm_load_ss(&m);
      eta = _mm_rsqrt_ss(eta);
      _mm_store_ss(&m, eta);
      float t = f->x * m;
#else
	  float t = f->x*InvSqrt(w[1] + g * f->x * f->x);
#endif
	  xGx += t * f->x;
	}
    }
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    {
      if (ec->atomics[(int)(*i)[0]].index() > 0)
	{
	  v_array<feature> temp = ec->atomics[(int)(*i)[0]];
	  for (; temp.begin != temp.end; temp.begin++)
	    xGx += xGx_quad(weights, *temp.begin, ec->atomics[(int)(*i)[1]], mask, g);
	} 
    }
  
  return xGx;
}

void inline_train(vw& all, example* &ec, float update)
{
  if (fabs(update) == 0.)
    return;
  size_t mask = all.weight_mask;
  weight* weights = all.reg.weight_vectors;
  for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
    {
      feature *f = ec->atomics[*i].begin;
      for (; f != ec->atomics[*i].end; f++){
	weights[f->weight_index & mask] += update * f->x;
      }
    }
  
  for (vector<string>::iterator i = all.pairs.begin(); i != all.pairs.end();i++) 
    {
      if (ec->atomics[(int)(*i)[0]].index() > 0)
	{
	  v_array<feature> temp = ec->atomics[(int)(*i)[0]];
	  for (; temp.begin != temp.end; temp.begin++)
	    one_pf_quad_update(weights, *temp.begin, ec->atomics[(int)(*i)[1]], mask, update);
	} 
    }
}

void train(weight* weights, const v_array<feature> &features, float update)
{
  if (fabs(update) > 0.)
    for (feature* j = features.begin; j != features.end; j++)
      weights[j->weight_index] += update * j->x;
}

void local_predict(vw& all, example* ec)
{
  label_data* ld = (label_data*)ec->ld;

  all.set_minmax(all.sd, ld->label);

  ec->final_prediction = finalize_prediction(all, ec->partial_prediction * all.sd->contraction);

  if(all.active_simulation){
    float k = ec->example_t - ld->weight;
    ec->revert_weight = all.loss->getRevertingWeight(all.sd, ec->final_prediction, all.eta/powf(k,all.power_t));
    float importance = query_decision(all, ec, k);
    if(importance > 0){
      all.sd->queries += 1;
      ld->weight *= importance;
    }
    else //do not query => do not train
      ld->label = FLT_MAX;
  }

  float t;
  if(all.active)
    t = all.sd->weighted_unlabeled_examples;
  else
    t = ec->example_t;

  ec->eta_round = 0;
  if (ld->label != FLT_MAX)
    {
      ec->loss = all.loss->getLoss(all.sd, ec->final_prediction, ld->label) * ld->weight;

      if (all.training && ec->loss > 0.)
	{
	  double eta_t;
	  float norm;
	  if (all.adaptive && all.exact_adaptive_norm) {
	    float magx = 0.;
	    if (all.power_t == 0.5)
	      norm = compute_xGx(all, ec);
	    else 
	      norm = compute_general_xGx(all, ec, all.power_t);
	    magx = powf(ec->total_sum_feat_sq, 1. - all.power_t);
	    eta_t = all.eta * norm / magx * ld->weight;
	  } else {
	    eta_t = all.eta / powf(t,all.power_t) * ld->weight;
	    if (all.nonormalize) 
	      {
		norm = 1.;
		eta_t *= ec->total_sum_feat_sq;
	      }
	    else
	      norm = ec->total_sum_feat_sq;
	  }
	  ec->eta_round = all.loss->getUpdate(ec->final_prediction, ld->label, eta_t, norm) / all.sd->contraction;

	  if (all.reg_mode && fabs(ec->eta_round) > 1e-8) {
	    double dev1 = all.loss->first_derivative(all.sd, ec->final_prediction, ld->label);
	    double eta_bar = (fabs(dev1) > 1e-8) ? (-ec->eta_round / dev1) : 0.0;
	    if (fabs(dev1) > 1e-8)
	      all.sd->contraction /= (1. + all.l2_lambda * eta_bar * norm);
	    all.sd->gravity += eta_bar * sqrt(norm) * all.l1_lambda;
	  }
	}
    }
  else if(all.active)
    ec->revert_weight = all.loss->getRevertingWeight(all.sd, ec->final_prediction, all.eta/powf(t,all.power_t));

  if (all.audit)
    print_audit_features(all, ec);
}

void predict(vw& all, example* ex)
{
  float prediction;
  if (all.reg_mode % 2)
    prediction = inline_predict_trunc(all, ex);
  else
    prediction = inline_predict(all, ex);

  ex->partial_prediction += prediction;

  local_predict(all, ex);
  ex->done = true;
}

void drive_gd(void* in)
{
  vw* all = (vw*)in;
  example* ec = NULL;
  
  while ( true )
    {
      if ((ec = get_example(all->p)) != NULL)//semiblocking operation.
	{
	  learn_gd(all, ec);
	  finish_example(*all, ec);
	}
      else if (parser_done(all->p))
	{
	  finish_gd(all);
	  return;
	}
      else 
	;//busywait when we have predicted on all examples but not yet trained on all.
    }
}
