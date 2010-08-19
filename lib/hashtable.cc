#include <iostream>
#include <list>
#include <queue>

#include "khmer.hh"
#include "hashtable.hh"
#include "parsers.hh"

#define CALLBACK_PERIOD 10000
#define PARTITION_ALL_TAG_DEPTH 500
#define PARTITION_MAX_TAG_EXAMINED 1e6

using namespace khmer;
using namespace std;

MinMaxTable * Hashtable::fasta_file_to_minmax(const std::string &inputfile,
					      unsigned int total_reads,
					      ReadMaskTable * readmask,
					      CallbackFn callback,
					      void * callback_data)
{
   IParser* parser = IParser::get_parser(inputfile.c_str());
   Read read;
   string seq = "";
   unsigned int read_num = 0;

   MinMaxTable * mmt = new MinMaxTable(total_reads);

   while(!parser->is_complete()) {
     read = parser->get_next_read();
     seq = read.seq;

     bool valid_read = true;
     if (!readmask || readmask->get(read_num)) {
       valid_read = check_read(seq);

       if (valid_read) {
         BoundedCounterType minval = get_min_count(seq);
	 BoundedCounterType maxval = get_max_count(seq);

	 mmt->add_min(read_num, minval);
	 mmt->add_max(read_num, maxval);
       }
     }

     seq.clear();
     read_num += 1;

     // run callback, if specified
     if (read_num % CALLBACK_PERIOD == 0 && callback) {
       try {
         callback("fasta_file_to_minmax", callback_data, read_num, 0);
       } catch (...) {
         delete mmt;
	 throw;
       }
     }
   }

  return mmt;
}

//
// filter_fasta_file_any: filters a FASTA file based on whether any (at
// least one) k-mer in a sequence has 'threshold' counts in the hashtable.
//

ReadMaskTable * Hashtable::filter_fasta_file_any(MinMaxTable &minmax,
						 BoundedCounterType threshold,
						 ReadMaskTable * old_readmask,
						 CallbackFn callback,
						 void * callback_data)

{
   unsigned int read_num;
   const unsigned int tablesize = minmax.get_tablesize();
   ReadMaskTable * readmask = new ReadMaskTable(tablesize);

   if (old_readmask) {
     readmask->merge(*old_readmask);
   }

   for (read_num = 0; read_num < tablesize; read_num++) {
     if (readmask->get(read_num)) {
       BoundedCounterType maxval = minmax.get_max(read_num);

       if (maxval < threshold) {
	 readmask->set(read_num, false);
       }

       // run callback, if specified
       if (read_num % CALLBACK_PERIOD == 0 && callback) {
	 try {
	   callback("filter_fasta_file_any", callback_data, read_num, 0);
	 } catch (...) {
	   delete readmask;
	   throw;
	 }
       }
     }
   }
  
   return readmask;
}

//
// filter_fasta_file_limit_n: filters a FASTA file based on whether
// a read has at least n kmers that meet a 'threshold' count
//

ReadMaskTable * Hashtable::filter_fasta_file_limit_n(const std::string &readsfile,
                                                     MinMaxTable &minmax,
                                                     BoundedCounterType threshold,
                                                     BoundedCounterType n,
                                                     ReadMaskTable * old_readmask,
                                                     CallbackFn callback,
                                                     void * callback_data)
{
   IParser* parser = IParser::get_parser(readsfile.c_str());
   string seq;
   Read read;
   unsigned int read_num = 0;
   const unsigned int tablesize = minmax.get_tablesize();

   ReadMaskTable * readmask = new ReadMaskTable(tablesize);

   if (old_readmask) {
     readmask->merge(*old_readmask);
   }

   while(!parser->is_complete()) {
      read = parser->get_next_read();
      seq = read.seq;
     
      if (readmask->get(read_num)) {
         int numPos = seq.length() - _ksize + 1;
         unsigned int n_met = 0;
 
         for (int i = 0; i < numPos; i++)  {
            string kmer = seq.substr(i, _ksize);
            if ((int)this->get_count(kmer.c_str()) >= threshold)  {
               n_met++;
            }
         }
 
         if (n_met < n)  {
            readmask->set(read_num, false);
         }
 
         read_num++;
 
         // run callback, if specified
         if (read_num % CALLBACK_PERIOD == 0 && callback) {
            try {
               callback("filter_fasta_file_limit_n", callback_data, read_num, 0);
            } catch (...) {
               delete readmask;
               throw;
            }
         }
      }
   }
         
   return readmask;
}

//
// filter_fasta_file_all: filters a FASTA file based on whether all
// k-mers in a sequence have 'threshold' counts in the hashtable.
//

ReadMaskTable * Hashtable::filter_fasta_file_all(MinMaxTable &minmax,
						 BoundedCounterType threshold,
						 ReadMaskTable * old_readmask,
						 CallbackFn callback,
						 void * callback_data)
{
   unsigned int read_num;
   const unsigned int tablesize = minmax.get_tablesize();

   ReadMaskTable * readmask = new ReadMaskTable(tablesize);

   if (old_readmask) {
     readmask->merge(*old_readmask);
   }

   for (read_num = 0; read_num < tablesize; read_num++) {
     if (readmask->get(read_num)) {
       BoundedCounterType minval = minmax.get_min(read_num);

       if (minval < threshold) {
	 readmask->set(read_num, false);
       }

       // run callback, if specified
       if (read_num % CALLBACK_PERIOD == 0 && callback) {
	 try {
	   callback("filter_fasta_file_all", callback_data, read_num, 0);
	 } catch (...) {
	   delete readmask;
	   throw;
	 }
       }
     }
   }
  
   return readmask;
}

//
// filter_fasta_file_run: filters a FASTA file based on whether a run of
// k-mers in a sequence al have 'threshold' counts in the hashtable.
//

ReadMaskTable * Hashtable::filter_fasta_file_run(const std::string &inputfile,
						 unsigned int total_reads,
						 BoundedCounterType threshold,
						 unsigned int runlength,
						 ReadMaskTable * old_readmask,
						 CallbackFn callback,
						 void * callback_data)

{
   IParser* parser = IParser::get_parser(inputfile.c_str());
   string seq;
   Read read;
   unsigned int read_num = 0;
   unsigned int n_kept = 0;
   ReadMaskTable * readmask = new ReadMaskTable(total_reads);

   if (old_readmask) {
     readmask->merge(*old_readmask);
   }

   while(parser->is_complete()) {
      read = parser->get_next_read();
      seq = read.seq;

      if (readmask->get(read_num)) {
         bool keep = false;
	   
         const unsigned int length = seq.length();
         const char * s = seq.c_str();
         unsigned int this_run = 0;

         for (unsigned int i = 0; i < length - _ksize + 1; i++) {
            HashIntoType count = this->get_count(s);
	    this_run++;
	    if (count < threshold) {
	       this_run = 0;
	    } else if (this_run >= runlength) {
	       keep = true;
	       break;
	    }
	    s++;
         }

         if (!keep) {
            readmask->set(read_num, false);
         } else {
            n_kept++;
         }
      }

      seq.clear();

      read_num += 1;

      // run callback, if specified
      if (read_num % CALLBACK_PERIOD == 0 && callback) {
         try {
            callback("filter_fasta_file_run", callback_data, read_num,n_kept);
         } catch (...) {
            throw;
         }
      }
   }

   return readmask;
}

///
/// output_fasta_kmer_pos_freq: outputs the kmer frequencies for each read
///

void Hashtable::output_fasta_kmer_pos_freq(const std::string &inputfile,
                                           const std::string &outputfile)
{
   IParser* parser = IParser::get_parser(inputfile.c_str());
   ofstream outfile;
   outfile.open(outputfile.c_str());
   string seq;
   Read read;

   while(!parser->is_complete()) {
      read = parser->get_next_read();
      seq = read.seq;

      int numPos = seq.length() - _ksize + 1;

      for (int i = 0; i < numPos; i++)  {
         string kmer = seq.substr(i, _ksize);
         outfile << (int)get_count(kmer.c_str()) << " ";
      }
      outfile << endl;
   }

   outfile.close();
}


unsigned int khmer::output_filtered_fasta_file(const std::string &inputfile,
					       const std::string &outputfile,
					       ReadMaskTable * readmask,
					       CallbackFn callback,
					       void * callback_data)
{
   IParser* parser = IParser::get_parser(inputfile.c_str());
   ofstream outfile;
   outfile.open(outputfile.c_str());
   Read read;
   string name;
   string seq;
   unsigned int n_kept = 0;
   unsigned int read_num = 0;


   while(!parser->is_complete()) {
      read = parser->get_next_read();

      seq = read.seq;
      name = read.name;

      if (readmask->get(read_num)) {
         outfile << ">" << name << endl;
	 outfile << seq << endl;

	 n_kept++;
      }

      name.clear();
      seq.clear();

      read_num++;

      // run callback, if specified
      if (read_num % CALLBACK_PERIOD == 0 && callback) {
         try {
            callback("output_filtered_fasta_file", callback_data,
		      read_num, n_kept);
	 } catch (...) {
	     outfile.close();
	     throw;
	 }
      }
   }
  
   outfile.close();
   return n_kept;
}

//
// check_and_process_read: checks for non-ACGT characters before consuming
//

unsigned int Hashtable::check_and_process_read(const std::string &read,
					    bool &is_valid,
                                            HashIntoType lower_bound,
                                            HashIntoType upper_bound)
{
   is_valid = check_read(read);

   if (!is_valid) { return 0; }

   return consume_string(read, lower_bound, upper_bound);
}

//
// check_read: checks for non-ACGT characters
//

bool Hashtable::check_read(const std::string &read)
{
   unsigned int i;
   bool is_valid = true;

   if (read.length() < _ksize) {
     is_valid = false;
     return 0;
   }

   for (i = 0; i < read.length(); i++)  {
     if (!is_valid_dna(read[i])) {
         is_valid = false;
	 return 0;
      }
   }

   return is_valid;
}

//
// consume_fasta: consume a FASTA file of reads
//

void Hashtable::consume_fasta(const std::string &filename,
			      unsigned int &total_reads,
			      unsigned long long &n_consumed,
			      HashIntoType lower_bound,
			      HashIntoType upper_bound,
			      ReadMaskTable ** orig_readmask,
			      bool update_readmask,
			      CallbackFn callback,
			      void * callback_data)
{
  total_reads = 0;
  n_consumed = 0;

  IParser* parser = IParser::get_parser(filename.c_str());
  Read read;

  string currName = "";
  string currSeq = "";

  //
  // readmask stuff: were we given one? do we want to update it?
  // 

  ReadMaskTable * readmask = NULL;
  std::list<unsigned int> masklist;

  if (orig_readmask && *orig_readmask) {
    readmask = *orig_readmask;
  }

  //
  // iterate through the FASTA file & consume the reads.
  //

  while(!parser->is_complete())  {
    read = parser->get_next_read();
    currSeq = read.seq;
    currName = read.name; 

    // do we want to process it?
    if (!readmask || readmask->get(total_reads)) {

      // yep! process.
      unsigned int this_n_consumed;
      bool is_valid;

      this_n_consumed = check_and_process_read(currSeq,
					       is_valid,
					       lower_bound,
					       upper_bound);

      // was this an invalid sequence -> mark as bad?
      if (!is_valid && update_readmask) {
        if (readmask) {
	  readmask->set(total_reads, false);
	} else {
	  masklist.push_back(total_reads);
	}
      } else {		// nope -- count it!
        n_consumed += this_n_consumed;
      }
    }
	       
    // reset the sequence info, increment read number
    total_reads++;

    // run callback, if specified
    if (total_reads % CALLBACK_PERIOD == 0 && callback) {
      try {
        callback("consume_fasta", callback_data, total_reads, n_consumed);
      } catch (...) {
        throw;
      }
    }
  }


  //
  // We've either updated the readmask in place, OR we need to create a
  // new one.
  //

  if (orig_readmask && update_readmask && readmask == NULL) {
    // allocate, fill in from masklist
    readmask = new ReadMaskTable(total_reads);

    list<unsigned int>::const_iterator it;
    for(it = masklist.begin(); it != masklist.end(); ++it) {
      readmask->set(*it, false);
    }
    *orig_readmask = readmask;
  }
}

//
// consume_string: run through every k-mer in the given string, & hash it.
//

unsigned int Hashtable::consume_string(const std::string &s,
				       HashIntoType lower_bound,
				       HashIntoType upper_bound)
{
  const char * sp = s.c_str();
  const unsigned int length = s.length();
  unsigned int n_consumed = 0;

  HashIntoType h = 0, r = 0;
  bool bounded = true;

  if (lower_bound == upper_bound && upper_bound == 0) {
    bounded = false;
  }
  
  HashIntoType bin = _hash(sp, _ksize, h, r);

  writelock_acquire();

  try {
    if (!bounded || (bin >= lower_bound && bin < upper_bound)) {
      bin = bin % _tablesize;
      if (_counts[bin] != MAX_COUNT) {
        _counts[bin]++;
      }
      n_consumed++;
    }
    
    for (unsigned int i = _ksize; i < length; i++) {
      // left-shift the previous hash over
      h = h << 2;
      
      // 'or' in the current nt
      h |= twobit_repr(sp[i]);
      
      // mask off the 2 bits we shifted over.
      h &= bitmask;
      
      // now handle reverse complement
      r = r >> 2;
      r |= (twobit_comp(sp[i]) << (_ksize*2 - 2));
      
      bin = uniqify_rc(h, r);
      
      if (!bounded || (bin >= lower_bound && bin < upper_bound)) {
        bin = bin % _tablesize;
        if (_counts[bin] != MAX_COUNT) {
          _counts[bin]++;
        }
        n_consumed++;
      }
    }
  } catch (...) {
    writelock_release();
    throw;
  }
  
  writelock_release();

  return n_consumed;
}


BoundedCounterType Hashtable::get_min_count(const std::string &s,
					    HashIntoType lower_bound,
					    HashIntoType upper_bound)
{
  const unsigned int length = s.length();
  const char * sp = s.c_str();
  BoundedCounterType min_count = MAX_COUNT, count;

  HashIntoType h = 0, r = 0;
  bool bounded = true;

  if (lower_bound == upper_bound && upper_bound == 0) {
    bounded = false;
  }

  HashIntoType bin;
  
  bin = _hash(sp, _ksize, h, r);
  if (!bounded || (bin >= lower_bound && bin < upper_bound)) {
    min_count = this->get_count(bin);
  }

  for (unsigned int i = _ksize; i < length; i++) {
    // left-shift the previous hash over
    h = h << 2;

    // 'or' in the current nt
    h |= twobit_repr(sp[i]);

    // mask off the 2 bits we shifted over.
    h &= bitmask;

    // now handle reverse complement
    r = r >> 2;
    r |= (twobit_comp(sp[i]) << (_ksize*2 - 2));

    bin = uniqify_rc(h, r);

    if (!bounded || (bin >= lower_bound && bin < upper_bound)) {
      count = this->get_count(bin);
    
      if (count < min_count) {
	min_count = count;
      }
    }
  }
  return min_count;
}

BoundedCounterType Hashtable::get_max_count(const std::string &s,
					    HashIntoType lower_bound,
					    HashIntoType upper_bound)
{
  const unsigned int length = s.length();
  const char * sp = s.c_str();
  BoundedCounterType max_count = 0, count;

  HashIntoType h = 0, r = 0;
  bool bounded = true;

  if (lower_bound == upper_bound && upper_bound == 0) {
    bounded = false;
  }

  HashIntoType bin = _hash(sp, _ksize, h, r);
  if (!bounded || (bin >= lower_bound && bin < upper_bound)) {
    max_count = this->get_count(bin);
  }

  for (unsigned int i = _ksize; i < length; i++) {
    // left-shift the previous hash over
    h = h << 2;

    // 'or' in the current nt
    h |= twobit_repr(sp[i]);

    // mask off the 2 bits we shifted over.
    h &= bitmask;

    // now handle reverse complement
    r = r >> 2;
    r |= (twobit_comp(sp[i]) << (_ksize*2-2));

    bin = uniqify_rc(h, r);
    if (!bounded || (bin >= lower_bound && bin < upper_bound)) {
      count = this->get_count(bin);

      if (count > max_count) {
	max_count = count;
      }
    }
  }
  return max_count;
}

HashIntoType * Hashtable::abundance_distribution() const
{
  HashIntoType * dist = new HashIntoType[256];
  HashIntoType i;
  
  for (i = 0; i < 256; i++) {
    dist[i] = 0;
  }

  for (i = 0; i < _tablesize; i++) {
    dist[_counts[i]]++;
  }

  return dist;
}

HashIntoType * Hashtable::fasta_count_kmers_by_position(const std::string &inputfile,
					     const unsigned int max_read_len,
					     ReadMaskTable * readmask,
					     BoundedCounterType limit_by_count,
					     CallbackFn callback,
					     void * callback_data)
{
   unsigned long long *counts = new unsigned long long[max_read_len];

   for (unsigned int i = 0; i < max_read_len; i++) {
     counts[i] = 0;
   }

   Read read;
   IParser* parser = IParser::get_parser(inputfile.c_str());
   string name;
   string seq;
   unsigned int read_num = 0;

   while(!parser->is_complete()) {
      read = parser->get_next_read();

      seq = read.seq;
      bool valid_read = true;
	 
      if (!readmask || readmask->get(read_num)) {
	valid_read = check_read(seq);

	if (valid_read) {
	  for (unsigned int i = 0; i < seq.length() - _ksize + 1; i++) {
	    string kmer = seq.substr(i, i + _ksize);
	    BoundedCounterType n = get_count(kmer.c_str());
	    
	    if (limit_by_count == 0 || n == limit_by_count) {
	      if (i < max_read_len) {
		counts[i]++;
	      }
	    }
	  }
	}
 
	name.clear();
	seq.clear();
      }

      read_num += 1;

      // run callback, if specified
      if (read_num % CALLBACK_PERIOD == 0 && callback) {
         try {
	    callback("fasta_file_count_kmers_by_position", callback_data, read_num, 0);
         } catch (...) {
	    throw;
         }
      }
   }

   return counts;
}

void Hashtable::fasta_dump_kmers_by_abundance(const std::string &inputfile,
					      ReadMaskTable * readmask,
					      BoundedCounterType limit_by_count,
					      CallbackFn callback,
					      void * callback_data)
{
  Read read;
  IParser* parser = IParser::get_parser(inputfile.c_str());
  string name;
  string seq;
  unsigned int read_num = 0;

  while(!parser->is_complete()) {
    read = parser->get_next_read();
    bool valid_read = true;
    seq = read.seq;

    if (!readmask || readmask->get(read_num)) {
      valid_read = check_read(seq);

      if (valid_read) {
        for (unsigned int i = 0; i < seq.length() - _ksize + 1; i++) {
	  string kmer = seq.substr(i, i + _ksize);
	  BoundedCounterType n = get_count(kmer.c_str());
	  char ss[_ksize + 1];
	  strncpy(ss, kmer.c_str(), _ksize);
	  ss[_ksize] = 0;

	  if (n == limit_by_count) {
            cout << ss << endl;
	  }
	}
      }

      name.clear();
      seq.clear();
    }

    read_num += 1;

    // run callback, if specified
    if (read_num % CALLBACK_PERIOD == 0 && callback) {
      try {
        callback("fasta_file_dump_kmers_by_abundance", callback_data, read_num, 0);
      } catch (...) {
        throw;
      }
    }
  }
}

//////////////////////////////////////////////////////////////////////
// graph stuff

ReadMaskTable * Hashtable::filter_file_connected(const std::string &est,
                                                 const std::string &readsfile,
                                                 unsigned int total_reads)
{
   unsigned int read_num = 0;
   unsigned int n_kept = 0;
   unsigned long long int cluster_size;
   ReadMaskTable * readmask = new ReadMaskTable(total_reads);
   IParser* parser = IParser::get_parser(readsfile.c_str());


   std::string first_kmer = est.substr(0, _ksize);
   SeenSet keeper;
   calc_connected_graph_size(first_kmer.c_str(),
                             cluster_size,
                             keeper);

   while(!parser->is_complete())
   {
      std::string seq = parser->get_next_read().seq;

      if (readmask->get(read_num))
      {
         bool keep = false;

         HashIntoType h = 0, r = 0, kmer;
         kmer = _hash(seq.substr(0, _ksize).c_str(), _ksize, h, r);
         kmer = uniqify_rc(h, r);

         SeenSet::iterator i = keeper.find(kmer);
         if (i != keeper.end()) {
            keep = true;
         }

         if (!keep) {
            readmask->set(read_num, false);
         } else {
            n_kept++;
         }
      }

      read_num++;
   }

   return readmask;
}

void Hashtable::calc_connected_graph_size(const HashIntoType kmer_f,
					  const HashIntoType kmer_r,
					  unsigned long long& count,
					  SeenSet& keeper,
					  const unsigned long long threshold)
const
{
  HashIntoType kmer = uniqify_rc(kmer_f, kmer_r);
  const BoundedCounterType val = _counts[kmer % _tablesize];

  if (val == 0) {
    return;
  }

  // have we already seen me? don't count; exit.
  SeenSet::iterator i = keeper.find(kmer);
  if (i != keeper.end()) {
    return;
  }

  // keep track of both seen kmers, and counts.
  keeper.insert(kmer);
  count += 1;

#if 0
  // @@@
  if (val != MAX_COUNT) {
    _counts[kmer % _tablesize] = val - 1;
  }
#endif // 0

  // are we past the threshold? truncate search.
  if (threshold && count >= threshold) {
    return;
  }

  // otherwise, explore in all directions.

  // NEXT.

  HashIntoType f, r;
  const unsigned int rc_left_shift = _ksize*2 - 2;

  f = ((kmer_f << 2) & bitmask) | twobit_repr('A');
  r = kmer_r >> 2 | (twobit_comp('A') << rc_left_shift);
  calc_connected_graph_size(f, r, count, keeper, threshold);

  f = ((kmer_f << 2) & bitmask) | twobit_repr('C');
  r = kmer_r >> 2 | (twobit_comp('C') << rc_left_shift);
  calc_connected_graph_size(f, r, count, keeper, threshold);

  f = ((kmer_f << 2) & bitmask) | twobit_repr('G');
  r = kmer_r >> 2 | (twobit_comp('G') << rc_left_shift);
  calc_connected_graph_size(f, r, count, keeper, threshold);

  f = ((kmer_f << 2) & bitmask) | twobit_repr('T');
  r = kmer_r >> 2 | (twobit_comp('T') << rc_left_shift);
  calc_connected_graph_size(f, r, count, keeper, threshold);

  // PREVIOUS.

  r = ((kmer_r << 2) & bitmask) | twobit_comp('A');
  f = kmer_f >> 2 | (twobit_repr('A') << rc_left_shift);
  calc_connected_graph_size(f, r, count, keeper, threshold);

  r = ((kmer_r << 2) & bitmask) | twobit_comp('C');
  f = kmer_f >> 2 | (twobit_repr('C') << rc_left_shift);
  calc_connected_graph_size(f, r, count, keeper, threshold);

  r = ((kmer_r << 2) & bitmask) | twobit_comp('G');
  f = kmer_f >> 2 | (twobit_repr('G') << rc_left_shift);
  calc_connected_graph_size(f, r, count, keeper, threshold);

  r = ((kmer_r << 2) & bitmask) | twobit_comp('T');
  f = kmer_f >> 2 | (twobit_repr('T') << rc_left_shift);
  calc_connected_graph_size(f, r, count, keeper, threshold);
}

void Hashtable::trim_graphs(const std::string infilename,
			    const std::string outfilename,
			    unsigned int min_size,
			    CallbackFn callback,
			    void * callback_data)
{
  IParser* parser = IParser::get_parser(infilename.c_str());
  unsigned int total_reads = 0;
  unsigned int reads_kept = 0;
  Read read;
  string seq;

  string line;
  ofstream outfile(outfilename.c_str());

  //
  // iterate through the FASTA file & consume the reads.
  //

  while(!parser->is_complete())  {
    read = parser->get_next_read();
    seq = read.seq;

    bool is_valid = check_read(seq);

    if (is_valid) {
      std::string first_kmer = seq.substr(0, _ksize);
      unsigned long long clustersize = 0;
      SeenSet keeper;
      calc_connected_graph_size(first_kmer.c_str(), clustersize, keeper,
				min_size);

      if (clustersize >= min_size) {
	outfile << ">" << read.name << endl;
	outfile << seq << endl;
	reads_kept++;
      }
    }
	       
    total_reads++;

    // run callback, if specified
    if (total_reads % CALLBACK_PERIOD == 0 && callback) {
      try {
	callback("trim_graphs", callback_data, total_reads, reads_kept);
      } catch (...) {
	delete parser;
	throw;
      }
    }
  }

  delete parser;
}

HashIntoType * Hashtable::graphsize_distribution(const unsigned int &max_size)
{
  HashIntoType * p = new HashIntoType[max_size];
  const unsigned char seen = 1 << 7;
  unsigned long long size;

  for (unsigned int i = 0; i < max_size; i++) {
    p[i] = 0;
  }

  for (HashIntoType i = 0; i < _tablesize; i++) {
    BoundedCounterType count = _counts[i];
    if (count && !(count & seen)) {
      std::string kmer = _revhash(i, _ksize);
      size = 0;

      SeenSet keeper;
      calc_connected_graph_size(kmer.c_str(), size, keeper, max_size);
      if (size) {
	if (size < max_size) {
	  p[size] += 1;
	}
      }
    }
  }

  return p;
}

// used by do_partition to explore an entire graph and assign the tagged
// kmers to a particular partition.

void Hashtable::partition_set_id(const HashIntoType kmer_f,
				 const HashIntoType kmer_r,
				 SeenSet& keeper,
				 unsigned int * partition_id)

{
  {
    HashIntoType kmer = uniqify_rc(kmer_f, kmer_r);
    const BoundedCounterType val = _counts[kmer % _tablesize];

    if (val == 0) {
      return;
    }

    // have we already seen me? don't count; exit.
    SeenSet::iterator i = keeper.find(kmer);
    if (i != keeper.end()) {
      return;
    }

    // keep track of seen kmers
    keeper.insert(kmer);

    // Is this a kmer-to-tag, and have we tagged it already?
    PartitionMap::iterator fi = partition_map.find(kmer_f);
    if (fi != partition_map.end()) {
      unsigned int * existing = partition_map[kmer_f];
      if (existing != NULL) {	// can eliminate once it works :) @CTB
	assert(existing == partition_id);
      } else {
	partition_map[kmer_f] = partition_id;
      }
    }

    fi = partition_map.find(kmer_r);
    if (fi != partition_map.end()) {
      unsigned int * existing = partition_map[kmer_r];
      if (existing != NULL) {
	assert(existing == partition_id);
      } else {
	partition_map[kmer_r] = partition_id;
      }
    }
  }

  // NEXT.

  HashIntoType f, r;
  const unsigned int rc_left_shift = _ksize*2 - 2;

  f = ((kmer_f << 2) & bitmask) | twobit_repr('A');
  r = kmer_r >> 2 | (twobit_comp('A') << rc_left_shift);
  partition_set_id(f, r, keeper, partition_id);

  f = ((kmer_f << 2) & bitmask) | twobit_repr('C');
  r = kmer_r >> 2 | (twobit_comp('C') << rc_left_shift);
  partition_set_id(f, r, keeper, partition_id);

  f = ((kmer_f << 2) & bitmask) | twobit_repr('G');
  r = kmer_r >> 2 | (twobit_comp('G') << rc_left_shift);
  partition_set_id(f, r, keeper, partition_id);

  f = ((kmer_f << 2) & bitmask) | twobit_repr('T');
  r = kmer_r >> 2 | (twobit_comp('T') << rc_left_shift);
  partition_set_id(f, r, keeper, partition_id);

  // PREVIOUS.

  r = ((kmer_r << 2) & bitmask) | twobit_comp('A');
  f = kmer_f >> 2 | (twobit_repr('A') << rc_left_shift);
  partition_set_id(f, r, keeper, partition_id);

  r = ((kmer_r << 2) & bitmask) | twobit_comp('C');
  f = kmer_f >> 2 | (twobit_repr('C') << rc_left_shift);
  partition_set_id(f, r, keeper, partition_id);

  r = ((kmer_r << 2) & bitmask) | twobit_comp('G');
  f = kmer_f >> 2 | (twobit_repr('G') << rc_left_shift);
  partition_set_id(f, r, keeper, partition_id);

  r = ((kmer_r << 2) & bitmask) | twobit_comp('T');
  f = kmer_f >> 2 | (twobit_repr('T') << rc_left_shift);
  partition_set_id(f, r, keeper, partition_id);
}

// do_exact_partition: simple partitioning, done once per cluster.
//   1) load in all the sequences, tagging the first kmer of each sequence.
//   2) then, for each tag, explore entre cluster & set tag
//         using partition_set_id.
//
// slow for big clusters, because it has to find all tagged k-mers in each
// cluster.  no provision for giving up and retagging.

unsigned int Hashtable::do_exact_partition(const std::string infilename,
					   CallbackFn callback,
					   void * callback_data)
{
  unsigned int total_reads = 0;
  unsigned int reads_kept = 0;

  IParser* parser = IParser::get_parser(infilename);
  Read read;
  string seq;

  while(!parser->is_complete())  {
    bool is_valid;
    read = parser->get_next_read();
    seq = read.seq;

    check_and_process_read(seq, is_valid);

    if (is_valid) {
      std::string first_kmer = seq.substr(0, _ksize);
      HashIntoType kmer_f = _hash_forward(first_kmer.c_str(), _ksize);

      partition_map[kmer_f] = NULL;
    }
	       
    total_reads++;

    // run callback, if specified
    if (total_reads % CALLBACK_PERIOD == 0 && callback) {
      try {
	callback("do_exact_partition", callback_data, total_reads, reads_kept);
      } catch (...) {
	delete parser;
	throw;
      }
    }
   }

  delete parser;

  // now, build the partition maps
   
  unsigned int next_partition_id = 1;
  for(PartitionMap::iterator i=partition_map.begin();
      i != partition_map.end();
      ++i) {

    HashIntoType kmer_f = (*i).first;
    unsigned int * partition_id = (*i).second;

    if (partition_id == NULL) {
      HashIntoType kmer_r;
      SeenSet keeper;

      std::string kmer_s = _revhash(kmer_f, _ksize);

      _hash(kmer_s.c_str(), _ksize, kmer_f, kmer_r);

      partition_id = new unsigned int;
      *partition_id = next_partition_id;
      next_partition_id++;

      partition_set_id(kmer_f, kmer_r, keeper, partition_id);
    }
  }

  return next_partition_id - 1;
}

// do_truncated_partition: progressive partitioning.
//   1) load all sequences, tagging first kmer of each
//   2) do a truncated BFS search for all connected tagged kmers & assign
//      partition ID.
//
// CTB note: for unlimited PARTITION_ALL_TAG_DEPTH, yields perfect clustering.

void Hashtable::do_truncated_partition(const std::string infilename,
				       CallbackFn callback,
				       void * callback_data)
{
  unsigned int total_reads = 0;

  IParser* parser = IParser::get_parser(infilename);
  Read read;
  string seq;
  bool is_valid;

  std::string first_kmer;
  HashIntoType kmer_f, kmer_r;
  SeenSet tagged_kmers;
  bool surrender;

  while(!parser->is_complete()) {
    read = parser->get_next_read();
    seq = read.seq;

    check_and_process_read(seq, is_valid);

    if (is_valid) {
      first_kmer = seq.substr(0, _ksize);
      _hash(first_kmer.c_str(), _ksize, kmer_f, kmer_r);

      // find all tagged kmers within range.
      tagged_kmers.clear();
      surrender = false;
      partition_find_all_tags(kmer_f, kmer_r, tagged_kmers, surrender);

      // assign the partition ID
      assign_partition_id(kmer_f, tagged_kmers, surrender);

      // reset the sequence info, increment read number
      total_reads++;

      // run callback, if specified
      if (total_reads % CALLBACK_PERIOD == 0 && callback) {
	try {
	  callback("do_truncated_partition/read", callback_data, total_reads,
		   next_partition_id);
	} catch (...) {
	  delete parser;
	  throw;
	}
      }
    }
  }

  delete parser;
}

unsigned int Hashtable::output_partitioned_file(const std::string infilename,
						const std::string outputfile,
						CallbackFn callback,
						void * callback_data)
{
  IParser* parser = IParser::get_parser(infilename);
  ofstream outfile(outputfile.c_str());

  unsigned int total_reads = 0;
  unsigned int reads_kept = 0;

  std::set<unsigned int> partitions;

  Read read;
  string seq;

  std::string first_kmer;
  HashIntoType kmer_f, kmer_r;

  while(!parser->is_complete()) {
    read = parser->get_next_read();
    seq = read.seq;

    if (check_read(seq)) {
      first_kmer = seq.substr(0, _ksize);
      _hash(first_kmer.c_str(), _ksize, kmer_f, kmer_r);

      PartitionID * partition_p = partition_map[kmer_f];
      PartitionID partition_id = *partition_p;

      PartitionSet::const_iterator ii;
      ii = surrender_set.find(partition_id);
      char surrender_flag = ' ';
      if (ii != surrender_set.end()) {
	surrender_flag = '*';
      }

      outfile << ">" << read.name << "\t" << partition_id
	      << surrender_flag << "\n" 
	      << seq << "\n";
      partitions.insert(partition_id);
	       
      // reset the sequence info, increment read number
      total_reads++;

      // run callback, if specified
      if (total_reads % CALLBACK_PERIOD == 0 && callback) {
	try {
	  callback("do_truncated_partition/output", callback_data,
		   total_reads, reads_kept);
	} catch (...) {
	  delete parser; parser = NULL;
	  outfile.close();
	  throw;
	}
      }
    }
  }

  delete parser; parser = NULL;

  return partitions.size();
}

PartitionID Hashtable::assign_partition_id(HashIntoType kmer_f,
					   SeenSet& tagged_kmers,
					   bool surrender)

{
  PartitionID * this_partition_p = NULL;
  PartitionID return_val = 0; 

  // did we find a tagged kmer?
  if (tagged_kmers.size() >= 1) {
    return_val = _reassign_partition_ids(tagged_kmers, kmer_f);
  } else {
    this_partition_p = new unsigned int;
    *this_partition_p = next_partition_id;
    partition_map[kmer_f] = this_partition_p;

    PartitionPtrSet * s = new PartitionPtrSet();
    s->insert(this_partition_p);
    reverse_pmap[next_partition_id] = s;

    return_val = next_partition_id;
    next_partition_id++;
  }

  if (surrender) {
    surrender_set.insert(return_val);
  }
  return return_val;
}

PartitionID Hashtable::_reassign_partition_ids(SeenSet& tagged_kmers,
					       const HashIntoType kmer_f)
{
  SeenSet::iterator it = tagged_kmers.begin();
  unsigned int * this_partition_p = partition_map[*it];

  partition_map[kmer_f] = this_partition_p;
  assert(this_partition_p != NULL);

  unsigned int min_partition_id = *this_partition_p;
  it++;

  for (; it != tagged_kmers.end(); ++it) {
    unsigned int pid = *(partition_map[*it]);
    if (pid < min_partition_id) {
      min_partition_id = pid;
    }
  }

  PartitionPtrSet * master = reverse_pmap[min_partition_id];
  for (it = tagged_kmers.begin(); it != tagged_kmers.end(); ++it) {
    unsigned int * partition_p = partition_map[*it];
    if (*partition_p != min_partition_id) {
      PartitionPtrSet * s = reverse_pmap[*partition_p];
      reverse_pmap.erase(*partition_p);
      
      PartitionID * pp2;
      for (PartitionPtrSet::const_iterator si = s->begin();
	   si != s->end(); si++) {
	pp2 = (*si);
	*pp2 = min_partition_id;
	master->insert(pp2);
      }
      delete s;
    }
  }

  if (*this_partition_p != min_partition_id) {
    PartitionPtrSet * s = reverse_pmap[*this_partition_p];
    reverse_pmap.erase(*this_partition_p);
      
    PartitionID * pp2;
    for (PartitionPtrSet::const_iterator si = s->begin();
	 si != s->end(); si++) {
      pp2 = (*si);
      *pp2 = min_partition_id;
      master->insert(pp2);
    }
    delete s;
  }

  return *this_partition_p;
}

bool Hashtable::_do_continue(const HashIntoType kmer,
			     const SeenSet& keeper)
{
  // have we already seen me? don't count; exit.
  SeenSet::iterator i = keeper.find(kmer);

  return (i == keeper.end());
}

void Hashtable::_checkpoint_partitionmap(string pmap_filename,
					 string surrender_filename)
{
  ofstream outfile(pmap_filename.c_str(), ios::binary);
  char buf[1000000];
  unsigned int n_bytes = 0;

  cout << "checkpointing..." << partition_map.size() << "\n";

  PartitionMap::const_iterator pi = partition_map.begin();
  for (; pi != partition_map.end(); pi++) {
    HashIntoType kmer;
    PartitionID p_id;

    kmer = pi->first;
    p_id = *(pi->second);

    memcpy(&buf[n_bytes], &kmer, sizeof(HashIntoType));
    n_bytes += sizeof(HashIntoType);
    memcpy(&buf[n_bytes], &p_id, sizeof(PartitionID));
    n_bytes += sizeof(PartitionID);

    if (n_bytes >= 1000000 - sizeof(HashIntoType) - sizeof(PartitionID)) {
      outfile.write(buf, n_bytes);
      n_bytes = 0;
    }
  }
  if (n_bytes) {
    outfile.write(buf, n_bytes);
  }
  outfile.close();

  ofstream surrenderfile(surrender_filename.c_str(), ios::binary);

  n_bytes = 0;
  for (PartitionSet::const_iterator ss = surrender_set.begin();
       ss != surrender_set.end(); ss++) {
    PartitionID p_id = *ss;
    
    memcpy(&buf[n_bytes], &p_id, sizeof(PartitionID));
    n_bytes += sizeof(PartitionID);

    if (n_bytes >= 1000000 - sizeof(PartitionID)) {
      surrenderfile.write(buf, n_bytes);
      n_bytes = 0;
    }
  }
  if (n_bytes) {
    surrenderfile.write(buf, n_bytes);
  }
  surrenderfile.close();
}
					 

void Hashtable::_load_partitionmap(string infilename,
				   string surrenderfilename)
{
  ifstream infile(infilename.c_str(), ios::binary);
  char buf[1000000];
  unsigned int n_bytes = 0;
  unsigned int loaded = 0;

  cout << "loading... " << infilename << "\n";

  HashIntoType kmer;
  PartitionID p_id;
  PartitionSet partitions;

  while (!infile.eof()) {
    infile.read(buf, 1000000);
    n_bytes = infile.gcount();

    for (unsigned int i = 0; i < n_bytes;) {
      // memcpy(&kmer, &buf[i], sizeof(HashIntoType));
      i += sizeof(HashIntoType);
      memcpy(&p_id, &buf[i], sizeof(PartitionID));
      i += sizeof(PartitionID);

      partitions.insert(p_id);

      loaded++;
    }
  }

  PartitionPtrMap ppmap;
  for (PartitionSet::const_iterator si = partitions.begin();
      si != partitions.end(); si++) {
    PartitionID * p = new PartitionID;
    *p = *(si);
    ppmap[*p] = p;

    PartitionPtrSet * s = new PartitionPtrSet();
    s->insert(p);
    reverse_pmap[*p] = s;
  }

  infile.clear();
  infile.seekg(0, ios::beg);

  while (!infile.eof()) {
    infile.read(buf, 1000000);
    n_bytes = infile.gcount();

    for (unsigned int i = 0; i < n_bytes;) {
      memcpy(&kmer, &buf[i], sizeof(HashIntoType));
      i += sizeof(HashIntoType);
      memcpy(&p_id, &buf[i], sizeof(PartitionID));
      i += sizeof(PartitionID);

      partition_map[kmer] = ppmap[p_id];
    }
  }

  cout << "loaded: " << loaded << "\n";

  ifstream surrenderfile(surrenderfilename.c_str(), ios::binary);
  n_bytes = 0;
  while (!infile.eof()) {
    infile.read(buf, 1000000);
    n_bytes = infile.gcount();

    for (unsigned int i = 0; i < n_bytes;) {
      memcpy(&p_id, &buf[i], sizeof(PartitionID));
      i += sizeof(PartitionID);

      surrender_set.insert(p_id);
    }
  }
}
					 

bool Hashtable::_is_tagged_kmer(const HashIntoType kmer_f,
				const HashIntoType kmer_r,
				HashIntoType& tagged_kmer)
{
  PartitionMap::const_iterator fi = partition_map.find(kmer_f);
  if (fi != partition_map.end()) {
    tagged_kmer = kmer_f;
    return true;
  }

  fi = partition_map.find(kmer_r);
  if (fi != partition_map.end()) {
    tagged_kmer = kmer_r;
    return true;
  }

  return false;
}
		     

// used by do_truncated_partition

void Hashtable::partition_find_all_tags(HashIntoType kmer_f,
					HashIntoType kmer_r,
					SeenSet& tagged_kmers,
					bool& surrender)
{
  HashIntoType tagged_kmer;
  if (_is_tagged_kmer(kmer_f, kmer_r, tagged_kmer)) {
    //std::cout << "already tagged! " << kmer_f << "\n";
    tagged_kmers.insert(tagged_kmer);
    return;
  }
  //std::cout << "no find: " << kmer_f << "\n";

  HashIntoType f, r;
  bool first = true;
  NodeQueue node_q;
  const unsigned int rc_left_shift = _ksize*2 - 2;
  unsigned int total = 0;

  SeenSet keeper;		// keep track of traversed kmers

  // start breadth-first search.

  node_q.push(kmer_f);
  node_q.push(kmer_r);

  while(!node_q.empty()) {
    total++;

    if (total > PARTITION_MAX_TAG_EXAMINED ||
      node_q.size() > PARTITION_ALL_TAG_DEPTH) {
      surrender = true;
      break;
    }

    kmer_f = node_q.front();
    node_q.pop();
    kmer_r = node_q.front();
    node_q.pop();

    HashIntoType kmer = uniqify_rc(kmer_f, kmer_r);
    if (!_do_continue(kmer, keeper)) {
      continue;
    }

    // keep track of seen kmers
    keeper.insert(kmer);

    // Is this a kmer-to-tag, and have we put this tag in a partition already?
    // Search no further in this direction.
    if (!first && _is_tagged_kmer(kmer_f, kmer_r, tagged_kmer)) {
      tagged_kmers.insert(tagged_kmer);
      continue;
    }

    //
    // Enqueue next set of nodes.
    //

    // NEXT.
    f = ((kmer_f << 2) & bitmask) | twobit_repr('A');
    r = kmer_r >> 2 | (twobit_comp('A') << rc_left_shift);
    if (get_count(uniqify_rc(f,r))) {
      node_q.push(f); node_q.push(r);
    }

    f = ((kmer_f << 2) & bitmask) | twobit_repr('C');
    r = kmer_r >> 2 | (twobit_comp('C') << rc_left_shift);
    if (get_count(uniqify_rc(f,r))) {
      node_q.push(f); node_q.push(r);
    }

    f = ((kmer_f << 2) & bitmask) | twobit_repr('G');
    r = kmer_r >> 2 | (twobit_comp('G') << rc_left_shift);
    if (get_count(uniqify_rc(f,r))) {
      node_q.push(f); node_q.push(r);
    }

    f = ((kmer_f << 2) & bitmask) | twobit_repr('T');
    r = kmer_r >> 2 | (twobit_comp('T') << rc_left_shift);
    if (get_count(uniqify_rc(f,r))) {
      node_q.push(f); node_q.push(r);
    }

    // PREVIOUS.
    r = ((kmer_r << 2) & bitmask) | twobit_comp('A');
    f = kmer_f >> 2 | (twobit_repr('A') << rc_left_shift);
    if (get_count(uniqify_rc(f,r))) {
      node_q.push(f); node_q.push(r);
    }

    r = ((kmer_r << 2) & bitmask) | twobit_comp('C');
    f = kmer_f >> 2 | (twobit_repr('C') << rc_left_shift);
    if (get_count(uniqify_rc(f,r))) {
      node_q.push(f); node_q.push(r);
    }
    
    r = ((kmer_r << 2) & bitmask) | twobit_comp('G');
    f = kmer_f >> 2 | (twobit_repr('G') << rc_left_shift);
    if (get_count(uniqify_rc(f,r))) {
      node_q.push(f); node_q.push(r);
    }

    r = ((kmer_r << 2) & bitmask) | twobit_comp('T');
    f = kmer_f >> 2 | (twobit_repr('T') << rc_left_shift);
    if (get_count(uniqify_rc(f,r))) {
      node_q.push(f); node_q.push(r);
    }

    first = false;
  }
}
