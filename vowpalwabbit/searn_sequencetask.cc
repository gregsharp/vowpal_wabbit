#include <iostream>
#include <float.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include "searn.h"
#include "gd.h"
#include "io.h"
#include "parser.h"
#include "constant.h"
#include "oaa.h"
#include "csoaa.h"
#include "searn_sequencetask.h"

namespace SequenceTask {
  SearnUtil::history_info hinfo;
  size_t seq_max_action = 1;
  size_t constant_pow_length = 0;
  size_t increment = 0;  // this is just for fake LDF

  struct seq_state {
    // global stuff -- common to any state in a trajectory
    example** ec_start;
    size_t    length;

    // trajectory-specific stuff
    size_t    pos;
    history   predictions;
    size_t    predictions_hash;
    float     cum_loss;

    // everything is zero based, so pos starts out at zero and is what
    // we will predict NEXT.  this means that when pos==length we're
    // done.
  };

  bool initialize(vw&all, std::vector<std::string>&opts, po::variables_map& vm)
  {
    SearnUtil::default_info(&hinfo);

    po::options_description desc("Searn[sequence] options");
    desc.add_options()
      ("searn_sequencetask_history",  po::value<size_t>(), "length of history to use")
      ("searn_sequencetask_features", po::value<size_t>(), "length of history to pair with observed features")
      ("searn_sequencetask_bigrams",                       "use bigrams from history")
      ("searn_sequencetask_bigram_features",               "use bigrams from history paired with observed features");

    po::parsed_options parsed = po::command_line_parser(opts).
      style(po::command_line_style::default_style ^ po::command_line_style::allow_guessing).
      options(desc).allow_unregistered().run();
    opts = po::collect_unrecognized(parsed.options, po::include_positional);
    po::store(parsed, vm);
    po::notify(vm);
    

    bool loaded_from_regressor = all.searn; //if all.searn is already true, then we already loaded these options from the regressor

    if( loaded_from_regressor ) {
      hinfo.length = all.searn_sequencetask_history;
      if( vm.count("searn_sequencetask_history") && hinfo.length != vm["searn_sequencetask_history"].as<size_t>() )
        std::cerr << "warning: you specified a different value for --searn_sequencetask_history than the one loaded from regressor. Pursuing with loaded value: " << hinfo.length << endl;

      hinfo.features = all.searn_sequencetask_features;
      if( vm.count("searn_sequencetask_features") && hinfo.features != vm["searn_sequencetask_features"].as<size_t>() )
        std::cerr << "warning: you specified a different value for --searn_sequencetask_features than the one loaded from regressor. Pursuing with loaded value: " << hinfo.features << endl;

      hinfo.bigrams = all.searn_sequencetask_bigrams;
      if( vm.count("searn_sequencetask_bigrams") && !hinfo.bigrams )
        std::cerr << "warning: you specified --searn_sequencetask_bigrams but loaded regressor not using bigrams. Pursuing without bigrams." << endl;

      hinfo.bigram_features = all.searn_sequencetask_bigram_features;
      if( vm.count("searn_sequencetask_bigram_features") && !hinfo.bigram_features )
        std::cerr << "warning: you specified --searn_sequencetask_bigram_features but loaded regressor not using bigram_features. Pursuing without bigram_features." << endl;

      seq_max_action = all.searn_nb_actions;
    }    
    else {
      if (vm.count("searn_sequencetask_bigrams"))          hinfo.bigrams = true;
      if (vm.count("searn_sequencetask_history"))          hinfo.length = vm["searn_sequencetask_history"].as<size_t>();
      if (vm.count("searn_sequencetask_bigram_features"))  hinfo.bigram_features = true;
      if (vm.count("searn_sequencetask_features"))         hinfo.features = vm["searn_sequencetask_features"].as<size_t>();

      seq_max_action = vm["searn"].as<size_t>();
    }

    //sync with values in all for when regressor will be saved
    all.searn_sequencetask_history = hinfo.length;
    all.searn_sequencetask_features = hinfo.features;
    all.searn_sequencetask_bigrams = hinfo.bigrams;
    all.searn_sequencetask_bigram_features = hinfo.bigram_features;

    constant_pow_length = 1;
    for (size_t i=0; i < hinfo.length; i++)
      constant_pow_length *= quadratic_constant;

    increment = (all.length() * all.stride + 132489)/seq_max_action;

    return true;
  }

  bool final(state s0)
  {
    seq_state* s = (seq_state*)s0;
    return s->pos >= s->length;
  }

  float loss(state s0)
  {
    return ((seq_state*)s0)->cum_loss;
  }

  void step(state s0, action a)
  {
    seq_state* s = (seq_state*)s0;

    s->cum_loss += (oracle(s0) == a) ? 0.0 : 1.0;

    if (hinfo.length > 0) {
      int old_val = s->predictions[0];
      s->predictions_hash -= old_val * constant_pow_length;
      s->predictions_hash += a;
      s->predictions_hash *= quadratic_constant;
      for (size_t i=1; i<hinfo.length; i++)
        s->predictions[i-1] = s->predictions[i];
      s->predictions[hinfo.length-1] = a;
    }

    s->pos = s->pos + 1;
  }

  action oracle(state s0)
  {
    seq_state* s = (seq_state*)s0;
    return ((OAA::mc_label*)s->ec_start[s->pos]->ld)->label;
  }

  state copy(state src0)
  {
    seq_state* src = (seq_state*)src0;
    seq_state* dst = (seq_state*)SearnUtil::calloc_or_die(1, sizeof(seq_state));
    //memcpy(dst, src, sizeof(seq_state));
    dst->ec_start = src->ec_start;
    dst->length   = src->length;
    dst->pos      = src->pos;
    dst->predictions_hash = src->predictions_hash;
    dst->cum_loss = src->cum_loss;
    dst->predictions = (history)SearnUtil::calloc_or_die(hinfo.length, sizeof(uint32_t));
    for (size_t t=0; t<hinfo.length; t++)
      dst->predictions[t] = src->predictions[t];

    //    cerr << "copy returning s = " << dst << endl;
    return (state)dst;
  }

  void finish(state s0)
  {
    seq_state* s = (seq_state*)s0;

    //    cerr << "finish    with s = " << s << " and s->predictions = " << s->predictions << endl;
    SearnUtil::free_it(s->predictions);
    SearnUtil::free_it(s);
  }

  bool is_test_example(example**ec, size_t N) {
    for (size_t n=0; n<N; n++) {
      if (((OAA::mc_label*)ec[n]->ld)->label < 0)
        return 1;
    }
    return 0;
  }


  void start_state_multiline(example**ec, size_t len, state*s0)
  {
    seq_state* s = (seq_state*)SearnUtil::calloc_or_die(1, sizeof(seq_state));

    s->ec_start = ec;
    s->length   = len;
    s->pos      = 0;
    s->cum_loss = 0.;

    s->predictions = (history)SearnUtil::calloc_or_die(hinfo.length, sizeof(uint32_t));
    for (size_t t=0; t<hinfo.length; t++)
      s->predictions[t] = 0;

    s->predictions_hash = 0;

    //    cerr << "ssml returning s = " << s << endl;

    *s0 = s;
  }


  void cs_example(vw&all, state s0, example*&ec, bool create)
  {
    seq_state* s = (seq_state*)s0;
    example* cur = s->ec_start[s->pos];
    if (create) {
      SearnUtil::add_history_to_example(all, &hinfo, cur, s->predictions);
      ec = cur;
    } else { // destroy
      SearnUtil::remove_history_from_example(all, &hinfo, cur);
      ec = NULL;
    }
  }

  size_t hash(state s0)
  {
    seq_state* s = (seq_state*)s0;
    return quadratic_constant * (s->pos + quadratic_constant * s->predictions_hash);
  }
  
  bool equivalent(state a0, state b0)
  {
    seq_state* a = (seq_state*)a0;
    seq_state* b = (seq_state*)b0;

    if (a->pos != b->pos) return false;
    if (a->predictions_hash != b->predictions_hash) return false;

    for (size_t i=0; i<hinfo.length; i++)
      if (a->predictions[i] != b->predictions[i])
        return false;

    return true;
  }

  using namespace std;
  string to_string(state s0, bool return_truth, vector<action> actions)
  {
    seq_state* s = (seq_state*)s0;
    stringstream ss;
    size_t len = s->length;

    if (return_truth) {
      for (size_t i=0; i<len; i++) {
        size_t l = ((OAA::mc_label*)s->ec_start[i]->ld)->label;
        if (i > 0) ss << ' ';
        ss << l;
      }
    } else {
      for (size_t i=0; i<actions.size(); i++) {
        if (i > len) {
          cerr << "warning (searn_sequencetask.to_string): list of actions is too long!  truncating from " << actions.size() << " to " << len << endl;
          break;
        }
        if (i > 0) ss << ' ';
        ss << actions[i];
      }
      if (actions.size() < len) {
        cerr << "warning (searn_sequencetask.to_string): list of actions is too short!  appending from " << actions.size() << " to " << len << endl;
        for (size_t i=actions.size(); i<len; i++) {
          if (i > 0) ss << ' ';
          ss << '0';
        }
      }
    }

    return ss.str();
  }

  // The following is just to test out LDF... we "fake" being an
  // LDF-based task.

  bool allowed(state s, action a)
  {
    return ((a >= 1) && (a <= seq_max_action));
  }

  void cs_ldf_example(vw& all, state s0, action a, example*&ec, bool create)
  {
    seq_state* s = (seq_state*)s0;
    example* cur = s->ec_start[s->pos];
    if (create) {
      ec = alloc_example(sizeof(OAA::mc_label));
      copy_example_data(ec, cur, sizeof(OAA::mc_label));
      OAA::default_label(ec->ld);
      SearnUtil::add_history_to_example(all, &hinfo, ec, s->predictions);
      update_example_indicies(all.audit, ec, increment * a);
    } else {
      dealloc_example(OAA::delete_label, *ec);
      free(ec);
      ec = NULL;
    }
  }
}
