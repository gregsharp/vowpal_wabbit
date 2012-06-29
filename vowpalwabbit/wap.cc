#include <float.h>
#include <math.h>
#include <stdio.h>

#include "wap.h"
#include "simple_label.h"
#include "cache.h"
#include "csoaa.h"
#include "oaa.h"

using namespace std;

namespace WAP {
  //nonreentrant
  size_t increment=0;

  void mirror_features(vw& all, example* ec, size_t offset1, size_t offset2)
  {
    for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
      {
        size_t original_length = ec->atomics[*i].index();
        //cerr << "original_length = " << original_length << endl;
        for (size_t j = 0; j < original_length; j++)
          {
            feature* f = &ec->atomics[*i][j];
            feature temp = {- f->x, f->weight_index + offset2};
            f->weight_index += offset1;
            push(ec->atomics[*i], temp);
          }
        ec->sum_feat_sq[*i] *= 2;
        //cerr << "final_length = " << ec->atomics[*i].index() << endl;
      }
    if (all.audit)
      {
        for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
          if (ec->audit_features[*i].begin != ec->audit_features[*i].end)
            {
              size_t original_length = ec->audit_features[*i].index();
              for (size_t j = 0; j < original_length; j++)
                {
                  audit_data* f = &ec->audit_features[*i][j];
                  char* new_space = NULL;
                  if (f->space != NULL)
                    {
                      new_space = (char*)calloc(strlen(f->space)+1,sizeof(char));
                      strcpy(new_space, f->space);
                    }
                  char* new_feature = (char*)calloc(strlen(f->feature)+2,sizeof(char));
                  strcpy(new_feature+1, f->feature);
                  *new_feature = '-';
                  audit_data temp = {new_space, new_feature, f->weight_index + offset2, - f->x, true};
                  f->weight_index += offset1;
                  push(ec->audit_features[*i], temp);
                }
              //cerr << "final_length = " << ec->audit_features[*i].index() << endl;
            }
      }
    ec->num_features *= 2;
    ec->total_sum_feat_sq *= 2;
    //cerr << "total_sum_feat_sq = " << ec->total_sum_feat_sq << endl;
  }

  void unmirror_features(vw& all, example* ec, size_t offset1, size_t offset2)
  {
    for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
      {
        ec->atomics[*i].end = ec->atomics[*i].begin+ec->atomics[*i].index()/2;
        feature* end = ec->atomics[*i].end;
        for (feature* f = ec->atomics[*i].begin; f!= end; f++)
          f->weight_index -= offset1;
        ec->sum_feat_sq[*i] /= 2;
      }
    if (all.audit)
      {
        for (size_t* i = ec->indices.begin; i != ec->indices.end; i++) 
          if (ec->audit_features[*i].begin != ec->audit_features[*i].end)
            {
              for (audit_data *f = ec->audit_features[*i].begin + ec->audit_features[*i].index()/2; f != ec->audit_features[*i].end; f++)
                {
                  if (f->space != NULL)
                    free(f->space);
                  free(f->feature);
                  f->alloced = false;
                }
              ec->audit_features[*i].end = ec->audit_features[*i].begin+ec->audit_features[*i].index()/2;
              for (audit_data *f = ec->audit_features[*i].begin; f != ec->audit_features[*i].end; f++)
                f->weight_index -= offset1;
            }
      }
    ec->num_features /= 2;
    ec->total_sum_feat_sq /= 2;
  }

  struct float_wclass
  {
    float v;
    CSOAA::wclass ci;
  };
  int fi_compare(const void *e1, const void* e2)
  {
    float_wclass* fi1 = (float_wclass*)e1;
    float_wclass* fi2 = (float_wclass*)e2;
    if (fi1->ci.x > fi2->ci.x)
      return 1;
    else if (fi1->ci.x < fi2->ci.x)
      return -1;
    else
      return 0;
  }
  int fi_compare_i(const void *e1, const void* e2)
  {
    float_wclass* fi1 = (float_wclass*)e1;
    float_wclass* fi2 = (float_wclass*)e2;
    if (fi1->ci.weight_index > fi2->ci.weight_index)
      return 1;
    else if (fi1->ci.weight_index < fi2->ci.weight_index)
      return -1;
    else
      return 0;
  }
  v_array<float_wclass> vs;

  void (*base_learner)(void*, example*) = NULL;

  void train(vw& all, example* ec)
  {
    CSOAA::label* ld = (CSOAA::label*)ec->ld;

    CSOAA::wclass* old_end = ld->costs.end;
    CSOAA::wclass* j = ld->costs.begin; 
    for (CSOAA::wclass *cl = ld->costs.begin; cl != ld->costs.end; cl ++)
      if (cl->x != FLT_MAX)
        *j++ = *cl;
    ld->costs.end = j;
  
    float score = FLT_MAX;
    vs.erase();
    for (CSOAA::wclass *cl = ld->costs.begin; cl != ld->costs.end; cl ++)
      {
        float_wclass temp = {0., *cl};
        if (temp.ci.x < score)
          score = temp.ci.x;
        push(vs, temp);
      }
  
    qsort(vs.begin, vs.index(), sizeof(float_wclass), fi_compare);
  
    for (size_t i = 0; i < ld->costs.index(); i++)
      {
        vs[i].ci.x -= score;
        if (i == 0)
          vs[i].v = 0.;
        else
          vs[i].v = vs[i-1].v + (vs[i].ci.x-vs[i-1].ci.x) / (float)i;
      }
  
    qsort(vs.begin, vs.index(), sizeof(float_wclass), fi_compare_i);

    for (size_t i = 0; i < ld->costs.index(); i++)
      for (size_t j = i+1; j < ld->costs.index(); j++)
        {
          label_data simple_temp;
          simple_temp.weight = fabsf(vs[i].v - vs[j].v);
          if (simple_temp.weight > 1e-5)
            {
              simple_temp.initial = 0.;
	    
              if (vs[i].v < vs[j].v)
                simple_temp.label = 1;
              else
                simple_temp.label = -1;
	    
              ec->ld = &simple_temp;
	    
              ec->partial_prediction = 0.;
              uint32_t myi = vs[i].ci.weight_index;
              uint32_t myj = vs[j].ci.weight_index;

              mirror_features(all, ec,(myi-1)*increment, (myj-1)*increment);

              base_learner(&all, ec);
              unmirror_features(all, ec,(myi-1)*increment, (myj-1)*increment);
            }
        }

    ld->costs.end = old_end;
    ec->ld = ld;
  }

  uint32_t test(vw& all, example* ec)
  {
    uint32_t prediction = 1;
    float score = -FLT_MAX;
  
    CSOAA::label* cost_label = (CSOAA::label*)ec->ld; 

    for (size_t i = 0; i < cost_label->costs.index(); i++)
      {
        label_data simple_temp;
        simple_temp.initial = 0.;
        simple_temp.weight = 0.;
        simple_temp.label = FLT_MAX;
        uint32_t myi = cost_label->costs[i].weight_index;
        if (myi!= 1)
          update_example_indicies(all.audit, ec, increment*(myi-1));
        ec->partial_prediction = 0.;
        ec->ld = &simple_temp;
        base_learner(&all, ec);
        if (myi != 1)
          update_example_indicies(all.audit, ec, -increment*(myi-1));
        if (ec->partial_prediction > score)
          {
            score = ec->partial_prediction;
            prediction = myi;
          }
        cost_label->costs[i].partial_prediction = -ec->partial_prediction;
      }
    return prediction;
  }

  void learn(void* a, example* ec)
  {
    vw* all = (vw*)a;
    CSOAA::label* cost_label = (CSOAA::label*)ec->ld;
    
    uint32_t prediction = test(*all, ec);

    ec->ld = cost_label;
    
    if (cost_label->costs.index() > 0)
      train(*all, ec);
    *(OAA::prediction_t*)&(ec->final_prediction) = prediction;
  }

  void drive_wap(void* in)
  {
    vw* all = (vw*)in;
    example* ec = NULL;
    while ( true )
      {
        if ((ec = get_example(all->p)) != NULL)//semiblocking operation.
          {
            base_learner(all, ec);
            CSOAA::output_example(*all, ec);
	    VW::finish_example(*all, ec);
          }
        else if (parser_done(all->p))
          {
            all->finish(all);
            return;
          }
        else 
          ;
      }
  }

  void parse_flags(vw& all, std::vector<std::string>&, po::variables_map& vm, size_t s)
  {
    *(all.p->lp) = CSOAA::cs_label_parser;
    all.sd->k = s;
    all.driver = drive_wap;
    base_learner = all.learn;
    all.learn = learn;
    increment = (all.length()/all.sd->k) * all.stride;
  }
}

namespace WAP_LDF {
  v_array<example*> ec_seq = v_array<example*>();
  size_t read_example_this_loop = 0;
  bool need_to_clear = true;

  void (*base_learner)(void*, example*) = NULL;
  void (*base_finish)(void*) = NULL;

  float test(vw& all, example* ec)
  {
    label *ld = (label*)ec->ld;
    label_data simple_temp;
    simple_temp.initial = 0.;
    simple_temp.weight = 0.;
    simple_temp.label = FLT_MAX;
    ec->partial_prediction = 0.;
    ec->ld = &simple_temp;
    base_learner(&all, ec);
    ec->ld = ld;
    return ec->partial_prediction;
  }

  void subtract_example(vw& all, example *ec, example *ecsub)
  {
    float norm_sq = 0.;
    size_t num_f = 0;
    for (size_t* i = ecsub->indices.begin; i != ecsub->indices.end; i++) {
      size_t feature_index = 0;
      for (feature *f = ecsub->atomics[*i].begin; f != ecsub->atomics[*i].end; f++) {
        feature temp = { -f->x, (uint32_t) (f->weight_index & all.parse_mask) };
        push(ec->atomics[wap_ldf_namespace], temp);
        norm_sq += f->x * f->x;
        num_f ++;

        if (all.audit) {
          if (! (ecsub->audit_features[*i].index() >= feature_index)) {
            audit_data b_feature = ecsub->audit_features[*i][feature_index];
            audit_data a_feature = { NULL, NULL, (uint32_t) (f->weight_index & all.parse_mask), -f->x, false };
            a_feature.space = b_feature.space;
            a_feature.feature = b_feature.feature;
            push(ec->audit_features[wap_ldf_namespace], a_feature);
            feature_index++;
          }
        }
      }
    }
    push(ec->indices, wap_ldf_namespace);
    ec->sum_feat_sq[wap_ldf_namespace] = norm_sq;
    ec->total_sum_feat_sq += norm_sq;
    ec->num_features += num_f;
  }

  void unsubtract_example(vw& all, example *ec)
  {
    if (ec->indices.index() == 0) {
      cerr << "internal error (bug): trying to unsubtract_example, but there are no namespaces!" << endl;
      return;
    }
    
    if (ec->indices.last() != wap_ldf_namespace) {
      cerr << "internal error (bug): trying to unsubtract_example, but either it wasn't added, or something was added after and not removed!" << endl;
      return;
    }

    ec->num_features -= ec->atomics[wap_ldf_namespace].index();
    ec->total_sum_feat_sq -= ec->sum_feat_sq[wap_ldf_namespace];
    ec->sum_feat_sq[wap_ldf_namespace] = 0;
    ec->atomics[wap_ldf_namespace].erase();
    if (all.audit) {
      if (ec->audit_features[wap_ldf_namespace].begin != ec->audit_features[wap_ldf_namespace].end) {
        for (audit_data *f = ec->audit_features[wap_ldf_namespace].begin; f != ec->audit_features[wap_ldf_namespace].end; f++) {
          if (f->alloced) {
            free(f->space);
            free(f->feature);
            f->alloced = false;
          }
        }
      }

      ec->audit_features[wap_ldf_namespace].erase();
    }
    ec->indices.decr();
  }
    


  void do_actual_learning(vw& all)
  {
    if (ec_seq.index() <= 0) return;  // nothing to do

    int K = ec_seq.index();
    float min_cost = FLT_MAX;
    v_array<float> predictions = v_array<float>();
    float max_score = -FLT_MAX;
    size_t prediction = 0;
    float prediction_cost = 0.;
    bool isTest = CSOAA_LDF::example_is_test(*ec_seq.begin);
    for (int k=0; k<K; k++) {
      example *ec = ec_seq.begin[k];
      label   *ld = (label*)ec->ld;

      if (ld->weight < min_cost) 
        min_cost = ld->weight;
      if (CSOAA_LDF::example_is_test(ec) != isTest) {
        isTest = true;
        cerr << "warning: wap_ldf got mix of train/test data; assuming test" << endl;
      }

      float pred  = test(all, ec);

      push(predictions, pred);
      if (pred > max_score) {
        max_score = pred;
        prediction = ld->label;
        prediction_cost = ld->weight;
      }
    }
    prediction_cost -= min_cost;

    // do actual learning
    for (int k1=0; k1<K; k1++) {
      example *ec1 = ec_seq.begin[k1];
      label   *ld1 = (label*)ec1->ld;
      for (int k2=k1+1; k2<K; k2++) {
        example *ec2 = ec_seq.begin[k2];
        label   *ld2 = (label*)ec2->ld;

        if (fabs(ld1->weight - ld2->weight) < 1e-6)
          continue;

        // learn
        label_data simple_label;
        simple_label.initial = 0.;
        simple_label.label = (ld1->weight < ld2->weight) ? 1.0 : -1.0;
        simple_label.weight = 1.;
        ec1->ld = &simple_label;
        ec1->partial_prediction = 0.;
        subtract_example(all, ec1, ec2);
        base_learner(&all, ec1);
        unsubtract_example(all, ec1);
        ec1->ld = ld1;
      }
      
      // fill in partial predictions
      ec1->partial_prediction = predictions.begin[k1];
      
      // restore label
    }
    predictions.erase();
    free(predictions.begin);

    // fill in test predictions
    for (int k=0; k<K; k++) {
      example *ec = ec_seq.begin[k];
      label   *ld = (label*)ec->ld;
      *(OAA::prediction_t*)&(ec->final_prediction) = (prediction == ld->label) ? 1 : 0;
    }
  }

  void output_example_seq(vw& all)
  {
    if (ec_seq.index() > 0) 
      for (example** ecc=ec_seq.begin; ecc!=ec_seq.end; ecc++)
        CSOAA_LDF::output_example(all, *ecc);
  }

  void clear_seq(vw& all)
  {
    if (ec_seq.index() > 0) 
      for (example** ecc=ec_seq.begin; ecc!=ec_seq.end; ecc++) {
	VW::finish_example(all, *ecc);
      }
    ec_seq.erase();
  }

  void learn(void* a, example *ec) {
    vw* all = (vw*)a;

    if (ec_seq.index() >= all->p->ring_size - 2) { // give some wiggle room
      cerr << "warning: length of sequence at " << ec->example_counter << " exceeds ring size; breaking apart" << endl;
      do_actual_learning(*all);
      need_to_clear = true;
    }

    if (need_to_clear) {
      ec_seq.erase();
      need_to_clear = false;
    }

    if (CSOAA_LDF::example_is_newline(ec)) {
      do_actual_learning(*all);
      CSOAA_LDF::global_print_newline(*all);
      push(ec_seq, ec);
      need_to_clear = true;
    } else {
      push(ec_seq, ec);
    }
  }

  void finish(void* a)
  {
    vw* all = (vw*)a;
    base_finish(all);
  }

  void drive_wap_ldf(void* a)
  {
    vw* all = (vw*)a;
    example* ec = NULL;
    read_example_this_loop = 0;
    need_to_clear = false;
    while (true) {
      if ((ec = get_example(all->p)) != NULL) { // semiblocking operation
        learn(all, ec);
        if (need_to_clear) {
          output_example_seq(*all);
          clear_seq(*all);
          need_to_clear = false;
        }
      } else if (parser_done(all->p)) {
        do_actual_learning(*all);
        output_example_seq(*all);
        finish(all);
        clear_seq(*all);
        if (ec_seq.begin != NULL)
          free(ec_seq.begin);
        return;
      }
    }
  }

  void parse_flags(vw& all, std::vector<std::string>&opts, po::variables_map& vm, size_t s)
  {
    *(all.p->lp) = OAA::mc_label_parser;

    if (all.add_constant) {
      cerr << "warning: turning off constant for label dependent features; use --noconstant" << endl;
      all.add_constant = false;
    }

    all.driver = drive_wap_ldf;
    base_learner = all.learn;
    all.learn = learn;
    base_finish = all.finish;
    all.finish = finish;
  }
}
