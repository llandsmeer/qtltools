/*Copyright (C) 2015 Olivier Delaneau, Halit Ongen, Emmanouil T. Dermitzakis
 
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.*/

#include "ase_data.h"


void ase_data::readGenotypes(string filename ,string olog) {

	timer current_timer;
	output_file fdo;

	int n_includedG = 0;
	int n_excludedG_mult = 0;
	int n_excludedG_snpv = 0;
	int n_excludedG_snpN = 0;
	int n_excludedG_void = 0;
	int n_excludedG_user = 0;
	int n_excludedG_impq = 0;
	int n_excludedG_impp = 0;
	int n_excludedG_homo = 0;
	int n_excludedG_miss = 0;
	int n_excludedG_blkl = 0;
	int n_excludedG_dupl = 0;
	int n_excludedG_nir = 0;
	int n_excludedG_wr = 0;
	int n_fixed_flipped = 0;
	int n_fixed_swapped = 0;

	vrb.title("Reading VCF [" + filename + "]");
	if (olog != ""){
		vrb.bullet("Writing failed variants to [" + olog + "]");
		fdo.open(olog);
		if (fdo.fail()) vrb.error("Cannot open file [" + olog +"]");
	}
	bcf_srs_t * sr =  bcf_sr_init();
	sr->collapse = COLLAPSE_NONE;

	//Jump to regions if necessary
	if (vcf_region.isSet()){
		if (bcf_sr_set_regions(sr, vcf_region.get().c_str(), 0) == -1) vrb.error("Failed to jump to region [" + vcf_region.get() + "]");
		else vrb.bullet("scanning region(s) [" + vcf_region.get() + "]");
	}else vrb.bullet("scanning full VCF file");

	//Add readers
	if(!(bcf_sr_add_reader (sr, filename.c_str()))) {
		switch (sr->errnum) {
		case not_bgzf: vrb.error("Not compressed with bgzip");
		case idx_load_failed: vrb.error("Impossible to load index file");
		case file_type_error: vrb.error("Unrecognized file format");
		default: vrb.error("Unknown error when opening");
		}
	}

	//Sample processing
	int index_sample = -1;
	unsigned int n_samples_in_file = bcf_hdr_nsamples(sr->readers[0].header);
	for (int i = 0 ; i < n_samples_in_file ; i ++) if (strcmp(sr->readers[0].header->samples[i], sample_id[0].c_str()) == 0) index_sample = i;
	if (index_sample < 0) vrb.error("Unexpected error: sample not found!");
	else vrb.bullet("index of [" + sample_id[0] + "] = " + stb.str(index_sample));

	//Init needed data
	int ngp = 0, ngt = 0, niq = 0, ngt_arr = 0, ngp_arr = 0, niq_arr = 0;
	int * gt_arr = NULL;
	float * gp_arr = NULL, * iq_arr = NULL;
	bcf1_t * line;

	unsigned int linecount = 0;
	while(bcf_sr_next_line (sr)) {
		linecount++;
		if (linecount % 1000000 == 0) vrb.bullet(stb.str(linecount) + " lines read");
		bool af = false;
		line =  bcf_sr_get_line(sr, 0);
		bcf_unpack(line, BCF_UN_STR);
		string sid = string(line->d.id);

		//filter multiallelic
		if (line->n_allele > 2) {n_excludedG_mult ++; if (olog != "") fdo << "VCF_MULTI_ALLELIC " << sid << endl; continue;}
		//filter user provided
		if (!filter_genotype.check(sid)) { n_excludedG_user ++; if (olog != "") fdo << "VCF_USER " << sid << endl; continue;}
		string curr_chr = bcf_hdr_id2name(sr->readers[0].header, line->rid);				//chr
		if(fix_chr){
			if (add_chr.count(curr_chr)) curr_chr = "chr" + curr_chr;
			if (remove_chr.count(curr_chr)) curr_chr = curr_chr.substr(3);
		}
		unsigned int pos = line->pos;	//position 0-based
		//filter blacklisted regions
		if(ase_basic_block(curr_chr,pos+1,pos+1).find_this_in_bool(blacklisted_regions)){n_excludedG_blkl++; if (olog != "") fdo << "VCF_BLACKLIST " << sid << endl; continue;}
		string ref = string(line->d.allele[0]);												//ref
		string alt = string(line->d.allele[1]);												//alt
		//filter indels
		if (ref.size() > 1 || alt.size() > 1) {n_excludedG_snpv ++; if (olog != "") fdo << "VCF_INDEL " << sid << endl; continue;}
		//filter missing ref alt alleles
		if (ref == "N" || alt == "N" || ref == "" || alt == "" || ref == "." || alt == ".") {n_excludedG_snpN++; if (olog != "") fdo << "VCF_MISSING_REF_ALT " << sid << endl; continue;}
		//filter not in fasta
		if (genome.size() && (genome.count(curr_chr) == 0 ||  pos >= genome[curr_chr].size())) {n_excludedG_nir++; if (olog != "") fdo << "VCF_NOT_IN_FASTA " << sid << endl; continue;}
		//filter ref mismatches
		if (genome.size() && ref[0] != genome[curr_chr][pos]) {
			if (auto_flip){
				if (alt[0] == genome[curr_chr][pos]){
					string tsr = ref;
					ref = alt;
					alt = tsr;
					//vrb.warning( curr_chr + ":" + stb.str(pos)+ ":" + alt + ref + ":" + sid + " was swapped to " + ref + alt);
					n_fixed_swapped++;
					if (olog != "") fdo << "VCF_SWAPPED " << sid << " " << alt + ref << " " << ref+alt << endl;
					af = true;
				}else if(complement(ref) == genome[curr_chr][pos]){
					string ola = ref + alt;
					ref[0] = complement(ref);
					alt[0] = complement(alt);
					n_fixed_flipped++;
					//vrb.warning( curr_chr + ":" + stb.str(pos)+ ":" + ola + ":" + sid + " was flipped to " + ref + alt);
					if (olog != "") fdo << "VCF_FLIPPED " << sid << " " << ola << " " << ref+alt <<  endl;
					af = true;
				}else{
					n_excludedG_wr++;
					if (olog != "") fdo << "VCF_WRONG_REF " << sid << endl;
					continue;
				}
			}else{
				n_excludedG_wr++;
				if (olog != "") fdo << "VCF_WRONG_REF " << sid << endl;
				continue;
			}
		}
		niq = bcf_get_info_float(sr->readers[0].header, line, param_imputation_score_label.c_str(), &iq_arr, &niq_arr);		//imputation score
		//filter imputation score
		if (niq > 0 && iq_arr[0] < param_min_iq) {n_excludedG_impq ++; if (olog != "") fdo << "VCF_BAD_IMPUTATION " << sid << endl; continue;}
		ngt = bcf_get_genotypes(sr->readers[0].header, line, &gt_arr, &ngt_arr); //genotypes
		ngp = bcf_get_format_float(sr->readers[0].header, line,param_genotype_likelihood_label.c_str(), &gp_arr, &ngp_arr); //genotype likelihoods
		//filter variants without the GT field
		if (ngt != n_samples_in_file * 2){n_excludedG_void ++; if (olog != "") fdo << "VCF_MISSING_GT " << sid << endl; continue;}
		//filter missing genotypes
		if (gt_arr[2*index_sample+0] == bcf_gt_missing || gt_arr[2*index_sample+1] == bcf_gt_missing) {n_excludedG_miss ++; if (olog != "") fdo << "VCF_MISSING_GENOTYPE " << sid << endl; continue;}
		//filter bad genotype quality
		if (ngp == 3 * n_samples_in_file && gp_arr[3*index_sample+0] != bcf_float_missing && gp_arr[3*index_sample+1] != bcf_float_missing && gp_arr[3*index_sample+2] != bcf_float_missing && gp_arr[3*index_sample+0] < param_min_gp && gp_arr[3*index_sample+1] < param_min_gp && gp_arr[3*index_sample+2] < param_min_gp) {n_excludedG_impp ++; if (olog != "") fdo << "VCF_BAD_GENOTYPE " << sid << endl; continue;}
		//filter homozygous
		if (bcf_gt_allele(gt_arr[2*index_sample+0]) == bcf_gt_allele(gt_arr[2*index_sample+1])) {n_excludedG_homo ++; if (olog != "") fdo << "VCF_HOMOZYGOUS " << sid << endl; continue;}
		ase_site ases(curr_chr, sid, pos, ref, alt);
		if(af) ases.concern += "RM,";
		auto cit = all_variants.find(ases);
		//filter duplicate sites
		if(cit != all_variants.end()){
			ase_site old = *cit;
			vrb.warning(ases.getName() + " was already seen as " + old.getName() + " ignoring this");
			n_excludedG_dupl++;
			if (olog != "") fdo << "VCF_DUPLICATE " << sid << endl;
		}else{
			if (ases.sid == "." || ases.sid == ""){
				if (fix_id){
					ases.sid = ases.chr + "_" + stb.str(ases.pos + 1) + "_" + ases.ref + ases.alt;
					vrb.warning("Missing id was changed to " + ases.sid);
				}else vrb.warning("Missing id for " + ases.getName());
			}
			all_variants.insert(ases);
			n_includedG ++;
		}
	}

	vrb.bullet(stb.str(n_includedG) + " heterozygous genotypes included");
	if (n_excludedG_mult > 0) vrb.bullet(stb.str(n_excludedG_mult) + " multi-allelic variants excluded");
	if (n_excludedG_user > 0) vrb.bullet(stb.str(n_excludedG_user) + " variants excluded by user");
	if (n_excludedG_blkl > 0) vrb.bullet(stb.str(n_excludedG_blkl) + " variants in blacklisted regions excluded");
	if (n_excludedG_snpv > 0) vrb.bullet(stb.str(n_excludedG_snpv) + " indels excluded");
	if (n_excludedG_snpN > 0) vrb.bullet(stb.str(n_excludedG_snpN) + " variants with missing ref/alt excluded");
	if (n_excludedG_nir  > 0) vrb.bullet(stb.str(n_excludedG_nir)  + " variants missing from FASTA excluded");
	if (n_fixed_swapped  > 0) vrb.bullet(stb.str(n_fixed_swapped)  + " variants where ref/alt was swapped");
	if (n_fixed_flipped  > 0) vrb.bullet(stb.str(n_fixed_flipped)  + " variants where ref/alt was flipped to the +ve strand");
	if (n_excludedG_wr   > 0) vrb.bullet(stb.str(n_excludedG_wr)   + " variants with ref mismatches excluded");
	if (n_excludedG_impq > 0) vrb.bullet(stb.str(n_excludedG_impq) + " badly imputed variants excluded");
	if (n_excludedG_impp > 0) vrb.bullet(stb.str(n_excludedG_impp) + " low probability genotypes excluded");
	if (n_excludedG_void > 0) vrb.bullet(stb.str(n_excludedG_void) + " variants without GT field excluded");
	if (n_excludedG_miss > 0) vrb.bullet(stb.str(n_excludedG_miss) + " missing genotypes excluded");
	if (n_excludedG_homo > 0) vrb.bullet(stb.str(n_excludedG_homo) + " homozygous genotypes excluded");
	if (n_excludedG_dupl > 0) vrb.bullet(stb.str(n_excludedG_dupl) + " duplicate variants excluded");
	if (all_variants.size() == 0) vrb.leave("Cannot find usable variants in target region!");

	if (n_excludedG_dupl || n_fixed_swapped || n_fixed_flipped || n_excludedG_wr || n_excludedG_nir) vrb.warning("There are " + stb.str(n_excludedG_dupl + n_fixed_swapped + n_fixed_flipped + n_excludedG_wr + n_excludedG_nir)+ " problematic genotypes in the VCF file. Please look at the excluded genotypes!");
	if (blacklisted_regions.size() && !n_excludedG_blkl) vrb.warning("No variants fall into the blacklisted regions!");

	free(gt_arr);
	bcf_sr_destroy(sr);

	vrb.bullet("Time taken: " + stb.str(current_timer.abs_time()) + " seconds");
}


