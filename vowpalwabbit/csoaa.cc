/*
Copyright (c) by respective owners including Yahoo!, Microsoft, and
individual contributors. All rights reserved.  Released under a BSD (revised)
license as described in the file LICENSE.
 */
#include <float.h>
#include <math.h>
#include <stdio.h>

#include "csoaa.h"
#include "simple_label.h"
#include "cache.h"
#include "oaa.h"
#include "v_hashmap.h"
#include "parse_example.h"

using namespace std;

namespace CSOAA {

  void name_value(substring &s, v_array<substring>& name, float &v)
  {
    tokenize(':', s, name);
    
    switch (name.index()) {
    case 0:
    case 1:
      v = 1.;
      break;
    case 2:
      v = float_of_substring(name[1]);
      if ( nanpattern(v))
	{
	  cerr << "error NaN value for: ";
	  cerr.write(name[0].begin, name[0].end - name[0].begin);
	  cerr << " terminating." << endl;
	  exit(1);
	}
      break;
    default:
      cerr << "example with a wierd name.  What is ";
      cerr.write(s.begin, s.end - s.begin);
      cerr << "\n";
    }
  }

  bool is_test_label(label* ld)
  {
    if (ld->costs.index() == 0)
      return true;
    for (unsigned int i=0; i<ld->costs.index(); i++)
      if (FLT_MAX != ld->costs[i].x)
        return false;
    return true;
  }
  
  char* bufread_label(label* ld, char* c, io_buf& cache)
  {
    size_t num = *(size_t *)c;
    ld->costs.erase();
    c += sizeof(size_t);
    size_t total = sizeof(wclass)*num;
    if (buf_read(cache, c, (int)total) < total) 
      {
        cout << "error in demarshal of cost data" << endl;
        return c;
      }
    for (size_t i = 0; i<num; i++)
      {
        wclass temp = *(wclass *)c;
        c += sizeof(wclass);
        push(ld->costs, temp);
      }
  
    return c;
  }

  size_t read_cached_label(shared_data*, void* v, io_buf& cache)
  {
    label* ld = (label*) v;
    ld->costs.erase();
    char *c;
    size_t total = sizeof(size_t);
    if (buf_read(cache, c, (int)total) < total) 
      return 0;
    c = bufread_label(ld,c, cache);
  
    return total;
  }

  float weight(void* v)
  {
    return 1.;
  }

  float initial(void* v)
  {
    return 0.;
  }

  char* bufcache_label(label* ld, char* c)
  {
    *(size_t *)c = ld->costs.index();
    c += sizeof(size_t);
    for (unsigned int i = 0; i< ld->costs.index(); i++)
      {
        *(wclass *)c = ld->costs[i];
        c += sizeof(wclass);
      }
    return c;
  }

  void cache_label(void* v, io_buf& cache)
  {
    char *c;
    label* ld = (label*) v;
    buf_write(cache, c, sizeof(size_t)+sizeof(wclass)*ld->costs.index());
    bufcache_label(ld,c);
  }

  void default_label(void* v)
  {
    label* ld = (label*) v;
    ld->costs.erase();
  }

  void delete_label(void* v)
  {
    label* ld = (label*)v;
    ld->costs.erase();
    free(ld->costs.begin);
  }

  bool substring_eq(substring ss, const char* str) {
    size_t len_ss  = ss.end - ss.begin;
    size_t len_str = strlen(str);
    if (len_ss != len_str) return false;
    return (strncmp(ss.begin, str, len_ss) == 0);
  }

  void parse_label(parser* p, shared_data* sd, void* v, v_array<substring>& words)
  {
    label* ld = (label*)v;

    ld->costs.erase();
    for (unsigned int i = 0; i < words.index(); i++) {
      wclass f = {0.,0,0.,0.};
      name_value(words[i], p->parse_name, f.x);
      
      if (p->parse_name.index() == 0)
        cerr << "invalid cost: specification -- no names!" << endl;
      else {
        if (substring_eq(p->parse_name[0], "shared")) {
          if (p->parse_name.index() == 1) {
            f.x = -1;
            f.weight_index = 0;
          } else
            cerr << "shared feature vectors should not have costs" << endl;
        } else if (substring_eq(p->parse_name[0], "label")) {
          if (p->parse_name.index() == 2) {
            f.weight_index = (size_t)f.x;
            f.x = -1;
          } else
            cerr << "label feature vectors must have label ids" << endl;
        } else {
          f.weight_index = 0;
          if (p->parse_name.index() == 1 || p->parse_name.index() == 2 || p->parse_name.index() == 3) {
            f.weight_index = hashstring(p->parse_name[0], 0);
            if (p->parse_name.index() == 1 && f.x >= 0)  // test examples are specified just by un-valued class #s
              f.x = FLT_MAX;

            if ((f.weight_index >= 1) && (f.weight_index <= sd->k) && (f.x >= 0)) {}  // normal example
            else if ((f.weight_index >= 1) && (f.weight_index <= sd->k) && (f.x <= -1)) {}   // label definition
            else if ((f.weight_index == 0) && (f.x <= -1)) {} // shared header
            else
              cerr << "invalid cost specification: " << f.weight_index << endl;
          } else 
            cerr << "malformed cost specification!" << endl;
        }
        push(ld->costs, f);
      }
    }

    if (words.index() == 0) {
      if (sd->k != (uint32_t)-1) {
        for (size_t i = 1; i <= sd->k; i++) {
          wclass f = {FLT_MAX, i, 0., 0.};
          push(ld->costs, f);
        }
      } else {
        //cerr << "ldf test examples must have possible labels listed" << endl;
        //exit(-1);
      }
    }
  }

  void print_update(vw& all, bool is_test, example *ec)
  {
    if ( /* (all.sd->weighted_examples > all.sd->old_weighted_examples) || */ (all.sd->weighted_examples > all.sd->dump_interval && !all.quiet && !all.bfgs))
      {
        char label_buf[32];
        if (is_test)
          strcpy(label_buf," unknown");
        else
          sprintf(label_buf," known");

        fprintf(stderr, "%-10.6f %-10.6f %8ld %8.1f   %s %8lu %8lu\n",
                all.sd->sum_loss/all.sd->weighted_examples,
                all.sd->sum_loss_since_last_dump / (all.sd->weighted_examples - all.sd->old_weighted_examples),
                (long int)all.sd->example_number,
                all.sd->weighted_examples,
                label_buf,
                (long unsigned int)*(OAA::prediction_t*)&ec->final_prediction,
                (long unsigned int)ec->num_features);
     
        all.sd->sum_loss_since_last_dump = 0.0;
        all.sd->old_weighted_examples = all.sd->weighted_examples;
        all.sd->dump_interval *= 2;
      }
  }

  void output_example(vw& all, example* ec)
  {
    label* ld = (label*)ec->ld;
    all.sd->weighted_examples += 1.;
    all.sd->total_features += ec->num_features;
    float loss = 0.;
    if (!is_test_label(ld))
      {//need to compute exact loss
        size_t pred = *(OAA::prediction_t*)&ec->final_prediction;

        float chosen_loss = FLT_MAX;
        float min = FLT_MAX;
        for (wclass *cl = ld->costs.begin; cl != ld->costs.end; cl ++) {
          if (cl->weight_index == pred)
            chosen_loss = cl->x;
          if (cl->x < min)
            min = cl->x;
        }
        if (chosen_loss == FLT_MAX)
          cerr << "warning: csoaa predicted an invalid class" << endl;

        loss = chosen_loss - min;
      }

    all.sd->sum_loss += loss;
    all.sd->sum_loss_since_last_dump += loss;
  
    for (size_t* sink = all.final_prediction_sink.begin; sink != all.final_prediction_sink.end; sink++)
      all.print((int)*sink, (float) (*(OAA::prediction_t*)&(ec->final_prediction)), 0, ec->tag);

    if (all.raw_prediction > 0) {
      string outputString;
      stringstream outputStringStream(outputString);
      for (unsigned int i = 0; i < ld->costs.index(); i++) {
        wclass cl = ld->costs[i];
        if (i > 0) outputStringStream << ' ';
        outputStringStream << cl.weight_index << ':' << cl.partial_prediction;
      }
      //outputStringStream << endl;
      all.print_text(all.raw_prediction, outputStringStream.str(), ec->tag);
    }

    all.sd->example_number++;

    print_update(all, is_test_label((label*)ec->ld), ec);
  }

  void (*base_learner)(void*, example*) = NULL;
  void (*base_finish)(void*) = NULL;

  void learn(void* a, example* ec) {
    vw* all = (vw*)a;
    label* ld = (label*)ec->ld;
    size_t prediction = 1;
    float score = FLT_MAX;
    size_t current_increment = 0;
    size_t increment = (all->length()/all->base_learner_nb_w) * all->stride;

    for (wclass *cl = ld->costs.begin; cl != ld->costs.end; cl ++)
      {
        size_t i = cl->weight_index;
	label_data simple_temp;
	simple_temp.initial = 0.;

	if (cl->x == FLT_MAX || !all->training)
	  {
	    simple_temp.label = FLT_MAX;
	    simple_temp.weight = 0.;
	  }
	else
	  {
	    simple_temp.label = cl->x;
	    simple_temp.weight = 1.;
	  }

	ec->ld = &simple_temp;

        size_t desired_increment = increment * (i-1);

        if (desired_increment != current_increment) {
	  update_example_indicies(all->audit, ec, desired_increment - current_increment);
          current_increment = desired_increment;
        }

	base_learner(all, ec);
        cl->partial_prediction = ec->partial_prediction;
	if (ec->partial_prediction < score || (ec->partial_prediction == score && i < prediction)) {
          score = ec->partial_prediction;
          prediction = i;
        }
	ec->partial_prediction = 0.;
      }
    ec->ld = ld;
    *(OAA::prediction_t*)&(ec->final_prediction) = (size_t)prediction;
    if (current_increment != 0)
      update_example_indicies(all->audit, ec, -current_increment);
  }

  void finish(void* a)
  {
    vw* all = (vw*)a;
    base_finish(all);
  }

  void drive_csoaa(void* in)
  {
    vw* all = (vw*)in;
    example* ec = NULL;
    while ( true )
      {
        if ((ec = get_example(all->p)) != NULL)//semiblocking operation.
          {
            learn(all, ec);
            output_example(*all, ec);
	    VW::finish_example(*all, ec);
          }
        else if (parser_done(all->p))
          {
            //            finish(all);
            return;
          }
        else 
          ;
      }
  }

  void parse_flags(vw& all, std::vector<std::string>&opts, po::variables_map& vm, po::variables_map& vm_file)
  {
    //first parse for number of actions
    size_t nb_actions = 0;
    if( vm_file.count("csoaa") ) { //if loaded options from regressor
      nb_actions = vm_file["csoaa"].as<size_t>();
      if( vm.count("csoaa") && vm["csoaa"].as<size_t>() != nb_actions ) //if csoaa was also specified in commandline, warn user if its different
        std::cerr << "warning: you specified a different number of actions through --csoaa than the one loaded from predictor. Pursuing with loaded value of: " << nb_actions << endl;
    }
    else {
      nb_actions = vm["csoaa"].as<size_t>();

      //append csoaa with nb_actions to options_from_file so it is saved to regressor later
      std::stringstream ss;
      ss << " --csoaa " << nb_actions;
      all.options_from_file.append(ss.str());
    }

    *(all.p->lp) = cs_label_parser;
    if (!all.is_noop)
      all.driver = drive_csoaa;

    all.sd->k = nb_actions;
    all.base_learner_nb_w *= nb_actions;

    base_learner = all.learn;
    all.base_learn = all.learn;
    all.learn = learn;
    base_finish = all.finish;
    all.finish = finish;
  }

  bool example_is_test(example* ec)
  {
    v_array<CSOAA::wclass> costs = ((label*)ec->ld)->costs;
    if (costs.index() == 0) return true;
    for (size_t j=0; j<costs.index(); j++)
      if (costs[j].x != FLT_MAX) return false;
    return true;    
  }

}

namespace CSOAA_AND_WAP_LDF {
  v_array<example*> ec_seq = v_array<example*>();
  size_t read_example_this_loop = 0;
  bool need_to_clear = true;
  bool is_singleline = true;
  bool is_wap = false;
  float csoaa_example_t = 0;

  void (*base_learner)(void*, example*) = NULL;
  void (*base_finish)(void*) = NULL;

  inline bool cmp_wclass_ptr(const CSOAA::wclass* a, const CSOAA::wclass* b) { return a->x < b->x; }

  void compute_wap_values(vector<CSOAA::wclass*> costs) {
    std::sort(costs.begin(), costs.end(), cmp_wclass_ptr);
    costs[0]->wap_value = 0.;
    for (size_t i=1; i<costs.size(); i++)
      costs[i]->wap_value = costs[i-1]->wap_value + (costs[i]->x - costs[i-1]->x) / (float)i;
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

  void make_single_prediction(vw& all, example*ec, size_t*prediction, float*min_score) {
    label   *ld = (label*)ec->ld;
    v_array<CSOAA::wclass> costs = ld->costs;
    label_data simple_label;

    for (size_t j=0; j<costs.index(); j++) {
      simple_label.initial = 0.;
      simple_label.label = FLT_MAX;
      simple_label.weight = 0.;
      ec->partial_prediction = 0.;

      LabelDict::add_example_namespace_from_memory(ec, costs[j].weight_index);
      
      ec->ld = &simple_label;
      base_learner(&all, ec); // make a prediction
      costs[j].partial_prediction = ec->partial_prediction;

      if (ec->partial_prediction < *min_score) {
        *min_score = ec->partial_prediction;
        *prediction = costs[j].weight_index;
      }

      LabelDict::del_example_namespace_from_memory(ec, costs[j].weight_index);
    }

    ec->ld = ld;
  }



  void do_actual_learning_wap(vw& all, int start_K)
  {
    size_t K = ec_seq.index();
    bool   isTest = CSOAA::example_is_test(ec_seq[start_K]);
    size_t prediction = 0;
    float  min_score = FLT_MAX;

    v_hashmap<size_t,float> hit_labels(8, 0., NULL);

    for (size_t k=start_K; k<K; k++) {
      example *ec = ec_seq.begin[k];

      if (CSOAA::example_is_test(ec) != isTest) {
        isTest = true;
        cerr << "warning: wap_ldf got mix of train/test data; assuming test" << endl;
      }
      if (LabelDict::ec_is_example_header(ec_seq[k])) {
        cerr << "warning: example headers at position " << k << ": can only have in initial position!" << endl;
        exit(-1);
      }

      make_single_prediction(all, ec, &prediction, &min_score);
    }

    // do actual learning
    vector<CSOAA::wclass*> all_costs;
    if (all.training && !isTest) {
      for (size_t k=start_K; k<K; k++) {
        v_array<CSOAA::wclass> this_costs = ((label*)ec_seq.begin[k]->ld)->costs;
        for (size_t j=0; j<this_costs.index(); j++)
          all_costs.push_back(&this_costs[j]);
      }
      compute_wap_values(all_costs);

      csoaa_example_t += 1.;
    }

    label_data simple_label;
    for (size_t k1=start_K; k1<K; k1++) {
      example *ec1 = ec_seq.begin[k1];
      label   *ld1 = (label*)ec1->ld;
      v_array<CSOAA::wclass> costs1 = ld1->costs;
      bool prediction_is_me = false;
      ec1->ld = &simple_label;
      float example_t1 = ec1->example_t;

      for (size_t j1=0; j1<costs1.index(); j1++) {
        if (costs1[j1].weight_index == (size_t)-1) continue;
        if (all.training && !isTest) {
          LabelDict::add_example_namespace_from_memory(ec1, costs1[j1].weight_index);

          for (size_t k2=k1+1; k2<K; k2++) {
            example *ec2 = ec_seq.begin[k2];
            label   *ld2 = (label*)ec2->ld;
            v_array<CSOAA::wclass> costs2 = ld2->costs;

            for (size_t j2=0; j2<costs2.index(); j2++) {
              if (costs2[j2].weight_index == (size_t)-1) continue;
              float value_diff = fabs(costs2[j2].wap_value - costs1[j1].wap_value);
              //float value_diff = fabs(costs2[j2].x - costs1[j1].x);
              if (value_diff < 1e-6)
                continue;

              LabelDict::add_example_namespace_from_memory(ec2, costs2[j2].weight_index);

              // learn
              ec1->example_t = csoaa_example_t;
              simple_label.initial = 0.;
              simple_label.label = (costs1[j1].x < costs2[j2].x) ? -1.0f : 1.0f;
              simple_label.weight = value_diff;
              ec1->partial_prediction = 0.;
              subtract_example(all, ec1, ec2);
              base_learner(&all, ec1);
              unsubtract_example(all, ec1);
              
              LabelDict::del_example_namespace_from_memory(ec2, costs2[j2].weight_index);
            }
          }
          LabelDict::del_example_namespace_from_memory(ec1, costs1[j1].weight_index);
        }

        if (prediction == costs1[j1].weight_index) prediction_is_me = true;
      }
      *(OAA::prediction_t*)&(ec1->final_prediction) = prediction_is_me ? prediction : 0;
      ec1->ld = ld1;
      ec1->example_t = example_t1;
    }
  }

  void do_actual_learning_oaa(vw& all, int start_K)
  {
    size_t K = ec_seq.index();
    size_t prediction = 0;
    bool   isTest = CSOAA::example_is_test(ec_seq[start_K]);
    float  min_score = FLT_MAX;

    for (size_t k=start_K; k<K; k++) {
      example *ec = ec_seq.begin[k];
      if (CSOAA::example_is_test(ec) != isTest) {
        isTest = true;
        cerr << "warning: ldf got mix of train/test data; assuming test" << endl;
      }
      if (LabelDict::ec_is_example_header(ec_seq[k])) {
        cerr << "warning: example headers at position " << k << ": can only have in initial position!" << endl;
        exit(-1);
      }
      make_single_prediction(all, ec, &prediction, &min_score);
    }

    // do actual learning
    if (all.training && !isTest)
      csoaa_example_t += 1.;
    for (size_t k=start_K; k<K; k++) {
      example *ec = ec_seq.begin[k];
      label   *ld = (label*)ec->ld;
      v_array<CSOAA::wclass> costs = ld->costs;

      // learn
      label_data simple_label;
      bool prediction_is_me = false;
      for (size_t j=0; j<costs.index(); j++) {
        if (all.training && !isTest) {
          float example_t = ec->example_t;
          ec->example_t = csoaa_example_t;
          simple_label.initial = 0.;
          simple_label.label = costs[j].x;
          simple_label.weight = 1.;
          ec->ld = &simple_label;
          ec->partial_prediction = 0.;
          LabelDict::add_example_namespace_from_memory(ec, costs[j].weight_index);
          base_learner(&all, ec);
          LabelDict::del_example_namespace_from_memory(ec, costs[j].weight_index);
          ec->example_t = example_t;
        }

        // fill in test predictions
        ec->partial_prediction = costs[j].partial_prediction;
        if (prediction == costs[j].weight_index) prediction_is_me = true;
      }
      *(OAA::prediction_t*)&(ec->final_prediction) = prediction_is_me ? prediction : 0;

      if (isTest && (costs.index() == 1)) {
        ec->final_prediction = costs[0].partial_prediction;
      }

      // restore label
      ec->ld = ld;
    }
  }


  void do_actual_learning(vw& all)
  {
    if (ec_seq.index() <= 0) return;  // nothing to do

    /////////////////////// handle label definitions
    if (LabelDict::ec_seq_is_label_definition(ec_seq)) {
      for (size_t i=0; i<ec_seq.index(); i++) {
        v_array<feature> features;
        for (feature*f=ec_seq[i]->atomics[ec_seq[i]->indices[0]].begin; f!=ec_seq[i]->atomics[ec_seq[i]->indices[0]].end; f++) {
          feature fnew = { f->x,  f->weight_index };
          push(features, fnew);
        }

        v_array<CSOAA::wclass> costs = ((CSOAA::label*)ec_seq[i]->ld)->costs;
        for (size_t j=0; j<costs.index(); j++) {
          size_t lab = costs[j].weight_index;
          LabelDict::set_label_features(lab, features);
        }
      }
      return;
    }

    /////////////////////// check for headers
    size_t K = ec_seq.index();
    size_t start_K = 0;
    if (LabelDict::ec_is_example_header(ec_seq[0])) {
      start_K = 1;
      for (size_t k=1; k<K; k++)
        LabelDict::add_example_namespaces_from_example(ec_seq[k], ec_seq[0]);
    }


    /////////////////////// learn
    if (is_wap) do_actual_learning_wap(all, start_K);
    else        do_actual_learning_oaa(all, start_K);

    /////////////////////// remove header
    if (start_K > 0)
      for (size_t k=1; k<K; k++)
        LabelDict::del_example_namespaces_from_example(ec_seq[k], ec_seq[0]);

  }

  void output_example(vw& all, example* ec, bool&hit_loss)
  {
    label* ld = (label*)ec->ld;
    v_array<CSOAA::wclass> costs = ld->costs;

    if (OAA::example_is_newline(ec)) return;
    if (LabelDict::ec_is_example_header(ec)) return;
    if (LabelDict::ec_is_label_definition(ec)) return;

    all.sd->total_features += ec->num_features;

    float loss = 0.;
    size_t final_pred = *(OAA::prediction_t*)&(ec->final_prediction);

    if (!CSOAA::example_is_test(ec)) {
      for (size_t j=0; j<costs.index(); j++) {
        if (hit_loss) break;
        if (final_pred == costs[j].weight_index) {
          loss = costs[j].x;
          hit_loss = true;
        }
      }

      all.sd->sum_loss += loss;
      all.sd->sum_loss_since_last_dump += loss;
      assert(loss >= 0);
    }
  
    for (size_t* sink = all.final_prediction_sink.begin; sink != all.final_prediction_sink.end; sink++)
      all.print(*sink, (float)(*(OAA::prediction_t*)&(ec->final_prediction)), 0, ec->tag);

    if (all.raw_prediction > 0) {
      string outputString;
      stringstream outputStringStream(outputString);
      for (size_t i = 0; i < costs.index(); i++) {
        if (i > 0) outputStringStream << ' ';
        outputStringStream << costs[i].weight_index << ':' << costs[i].partial_prediction;
      }
      //outputStringStream << endl;
      all.print_text(all.raw_prediction, outputStringStream.str(), ec->tag);
    }
    

    CSOAA::print_update(all, CSOAA::example_is_test(ec), ec);
  }

  void output_example_seq(vw& all)
  {
    if ((ec_seq.index() > 0) && !LabelDict::ec_seq_is_label_definition(ec_seq)) {
      all.sd->weighted_examples += 1;
      all.sd->example_number++;

      bool hit_loss = false;
      for (example** ecc=ec_seq.begin; ecc!=ec_seq.end; ecc++)
        output_example(all, *ecc, hit_loss);

      if (all.raw_prediction > 0)
        all.print_text(all.raw_prediction, "", ec_seq[0]->tag);
    }
  }

  void clear_seq(vw& all)
  {
    if (ec_seq.index() > 0) 
      for (example** ecc=ec_seq.begin; ecc!=ec_seq.end; ecc++)
        VW::finish_example(all, *ecc);
    ec_seq.erase();
  }


  void learn_singleline(vw*all, example*ec) {
    if ((!all->training) || CSOAA::example_is_test(ec)) {
      size_t prediction = 0;
      float  min_score = FLT_MAX;
      make_single_prediction(*all, ec, &prediction, &min_score);
    } else {
      ec_seq.erase();
      push(ec_seq, ec);
      do_actual_learning(*all);
      ec_seq.erase();
    }
  }

  void learn_multiline(vw*all, example *ec) {
    if (ec_seq.index() >= all->p->ring_size - 2) { // give some wiggle room
      if (ec_seq[0]->pass == 0)
        cerr << "warning: length of sequence at " << ec->example_counter << " exceeds ring size; breaking apart" << endl;
      do_actual_learning(*all);
      need_to_clear = true;
    }

    if (need_to_clear) {
      output_example_seq(*all);
      clear_seq(*all);
      need_to_clear = false;
    }

    if (OAA::example_is_newline(ec)) {
      do_actual_learning(*all);
      if (!LabelDict::ec_seq_is_label_definition(ec_seq))
        global_print_newline(*all);
      VW::finish_example(*all, ec);
      need_to_clear = true;
    } else if (LabelDict::ec_is_label_definition(ec)) {
      if (ec_seq.index() > 0)
        cerr << "warning: label definition encountered in data block -- ignoring data!" << endl;
      learn_singleline(all, ec);
      VW::finish_example(*all, ec);
    } else {
      push(ec_seq, ec);
    }
  }

  void learn(void*a, example*ec) {
    vw* all = (vw*)a;
    if (is_singleline) learn_singleline(all, ec);
    else learn_multiline(all, ec);
  }

  void finish(void* a)
  {
    vw* all = (vw*)a;
    base_finish(all);
    LabelDict::free_label_features();
  }

  void drive_ldf_singleline(vw*all) {
    example* ec = NULL;
    while (true) {
      if ((ec = get_example(all->p)) != NULL) { //semiblocking operation.

        if (LabelDict::ec_is_example_header(ec)) {
          cerr << "error: example headers not allowed in ldf singleline mode" << endl;
          exit(-1);
        }
        learn_singleline(all, ec);
        if (! LabelDict::ec_is_label_definition(ec)) {
          all->sd->weighted_examples += 1;
          all->sd->example_number++;
        }
        bool hit_loss = false;
        output_example(*all, ec, hit_loss);
        VW::finish_example(*all, ec);
      } else if (parser_done(all->p)) {
        return;
      }
    }
  }

  void drive_ldf_multiline(vw*all) {
    example* ec = NULL;
    read_example_this_loop = 0;
    need_to_clear = false;
    while (true) {
      if ((ec = get_example(all->p)) != NULL) { // semiblocking operation
        learn_multiline(all, ec);
        if (need_to_clear) {
          output_example_seq(*all);
          clear_seq(*all);
          need_to_clear = false;
        }
      } else if (parser_done(all->p)) {
        do_actual_learning(*all);
        output_example_seq(*all);
        clear_seq(*all);
        if (ec_seq.begin != NULL)
          free(ec_seq.begin);
        return;
      }
    }
  }

  void drive_ldf(void*in)
  {
    vw* all =(vw*)in;
    if (is_singleline)
      drive_ldf_singleline(all);
    else
      drive_ldf_multiline(all);
  }


  void parse_flags(vw& all, std::vector<std::string>&opts, po::variables_map& vm, po::variables_map& vm_file)
  {
    string ldf_arg;
    if(vm_file.count("csoaa_ldf")) {
      ldf_arg = vm_file["csoaa_ldf"].as<string>();
      
      if(vm.count("csoaa_ldf") && ldf_arg.compare(vm["csoaa_ldf"].as<string>()) != 0)
        std::cerr << "warning: you specified a different ldf argument through --csoaa_ldf than the one loaded from regressor. Pursuing with loaded value of: " << ldf_arg << endl;
    }
    else if( vm.count("csoaa_ldf") ){
      ldf_arg = vm["csoaa_ldf"].as<string>();
      all.options_from_file.append(" --csoaa_ldf ");
      all.options_from_file.append(ldf_arg);
    }
    else if( vm_file.count("wap_ldf") ) {
      ldf_arg = vm_file["wap_ldf"].as<string>();
      is_wap = true;
      
      if(vm.count("wap_ldf") && ldf_arg.compare(vm["wap_ldf"].as<string>()) != 0)
        std::cerr << "warning: you specified a different value for --wap_ldf than the one loaded from regressor. Pursuing with loaded value of: " << ldf_arg << endl;
    }
    else {
      ldf_arg = vm["wap_ldf"].as<string>();
      is_wap = true;
      all.options_from_file.append(" --wap_ldf ");
      all.options_from_file.append(ldf_arg);
    }

    *(all.p->lp) = CSOAA::cs_label_parser;

    all.sd->k = (uint32_t)-1;

    if (ldf_arg.compare("singleline") == 0 || ldf_arg.compare("s") == 0)
      is_singleline = true;
    else if (ldf_arg.compare("multiline") == 0 || ldf_arg.compare("m") == 0)
      is_singleline = false;
    else {
      cerr << "ldf requires either [s]ingleline or [m]ultiline argument" << endl;
      exit(-1);
    }

    if (all.add_constant) {
      all.add_constant = false;
    }

    if (!all.is_noop)
      all.driver = drive_ldf;
    base_learner = all.learn;
    all.base_learn = all.learn;
    all.learn = learn;
    base_finish = all.finish;
    all.finish = finish;
  }

  void global_print_newline(vw& all)
  {
    char temp[1];
    temp[0] = '\n';
    for (size_t i=0; i<all.final_prediction_sink.index(); i++) {
      int f = all.final_prediction_sink[i];
      ssize_t t = write(f, temp, 1);
      if (t != 1)
        std::cerr << "write error" << std::endl;
    }
  }

}


namespace LabelDict {
  bool size_t_eq(size_t a, size_t b) { return (a==b); }
  v_hashmap< size_t, v_array<feature> > label_features(256, v_array<feature>(), size_t_eq);

  size_t hash_lab(size_t lab) { return 328051 + 94389193 * lab; }
  
  bool ec_is_label_definition(example*ec) // label defs look like "___:-1"
  {
    v_array<CSOAA::wclass> costs = ((CSOAA::label*)ec->ld)->costs;
    for (size_t j=0; j<costs.index(); j++)
      if (costs[j].x >= 0.) return false;
    if (ec->indices.index() == 0) return false;
    if (ec->indices.index() >  2) return false;
    if (ec->indices[0] != 'l') return false;
    return true;    
  }

  bool ec_is_example_header(example*ec)  // example headers look like "0:-1"
  {
    v_array<CSOAA::wclass> costs = ((CSOAA::label*)ec->ld)->costs;
    if (costs.index() != 1) return false;
    if (costs[0].weight_index != 0) return false;
    if (costs[0].x >= 0) return false;
    return true;    
  }

  bool ec_seq_is_label_definition(v_array<example*>ec_seq)
  {
    if (ec_seq.index() == 0) return false;
    bool is_lab = ec_is_label_definition(ec_seq[0]);
    for (size_t i=1; i<ec_seq.index(); i++) {
      if (is_lab != ec_is_label_definition(ec_seq[i])) {
        if (!((i == ec_seq.index()-1) && (OAA::example_is_newline(ec_seq[i])))) {
          cerr << "error: mixed label definition and examples in ldf data!" << endl;
          exit(-1);
        }
      }
    }
    return is_lab;
  }

  void del_example_namespace(example*ec, char ns, v_array<feature> features) {
    size_t numf = features.index();
    ec->num_features -= numf;

    assert (ec->atomics[(size_t)ns].index() >= numf);
    if (ec->atomics[(size_t)ns].index() == numf) { // did NOT have ns
      assert(ec->indices.index() > 0);
      assert(ec->indices[ec->indices.index()-1] == (size_t)ns);
      ec->indices.pop();
      ec->total_sum_feat_sq -= ec->sum_feat_sq[(size_t)ns];
      ec->atomics[(size_t)ns].erase();
      ec->sum_feat_sq[(size_t)ns] = 0.;
    } else { // DID have ns
      for (feature*f=features.begin; f!=features.end; f++) {
        ec->sum_feat_sq[(size_t)ns] -= f->x * f->x;
        ec->atomics[(size_t)ns].pop();
      }
    }
  }

  void add_example_namespace(example*ec, char ns, v_array<feature> features) {
    bool has_ns = false;
    for (size_t i=0; i<ec->indices.index(); i++) {
      if (ec->indices[i] == (size_t)ns) {
        has_ns = true;
        break;
      }
    }
    if (has_ns) {
      ec->total_sum_feat_sq -= ec->sum_feat_sq[(size_t)ns];
    } else {
      push(ec->indices, (size_t)ns);
      ec->sum_feat_sq[(size_t)ns] = 0;
    }

    for (feature*f=features.begin; f!=features.end; f++) {
      ec->sum_feat_sq[(size_t)ns] += f->x * f->x;
      push(ec->atomics[(size_t)ns], *f);
    }

    ec->num_features += features.index();
    ec->total_sum_feat_sq += ec->sum_feat_sq[(size_t)ns];
  }



  void add_example_namespaces_from_example(example*target, example*source) {
    for (size_t*idx=source->indices.begin; idx!=source->indices.end; idx++) {
      if (*idx == constant_namespace) continue;
      add_example_namespace(target, (char)*idx, source->atomics[*idx]);
    }
  }

  void del_example_namespaces_from_example(example*target, example*source) {
    //for (size_t*idx=source->indices.begin; idx!=source->indices.end; idx++) {
    size_t*idx = source->indices.end;
    idx--;
    for (; idx>=source->indices.begin; idx--) {
      if (*idx == constant_namespace) continue;
      del_example_namespace(target, (char)*idx, source->atomics[*idx]);
    }
  }

  void add_example_namespace_from_memory(example*ec, size_t lab) {
    size_t lab_hash = hash_lab(lab);
    v_array<feature> features = label_features.get(lab, lab_hash);
    if (features.index() == 0) return;
    add_example_namespace(ec, 'l', features);
  }

  void del_example_namespace_from_memory(example* ec, size_t lab) {
    size_t lab_hash = hash_lab(lab);
    v_array<feature> features = label_features.get(lab, lab_hash);
    if (features.index() == 0) return;
    del_example_namespace(ec, 'l', features);
  }

  void set_label_features(size_t lab, v_array<feature>features) {
    size_t lab_hash = hash_lab(lab);
    if (label_features.contains(lab, lab_hash)) { return; }
    label_features.put_after_get(lab, lab_hash, features);
  }

  void free_label_features() {
    void* label_iter = LabelDict::label_features.iterator();
    while (label_iter != NULL) {
      v_array<feature> features = LabelDict::label_features.iterator_get_value(label_iter);
      features.erase();
      free(features.begin);

      label_iter = LabelDict::label_features.iterator_next(label_iter);
    }
  }
}
