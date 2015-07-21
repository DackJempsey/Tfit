import time, load, merge_data_types as mdt
import sys, BIC
		
def output(I,  FHW, penality,diff_threshold ):
	model 	= BIC.get_best_model(I, penality , diff_threshold)
	FHW.write("#" + I.chrom + ":" + str(I.start) + "-" + str(I.stop) + "\n")
	for rv in model.rvs:
		FHW.write(rv.__str__()+"\n")

def run(merged_file, out_file_name, penality,diff_threshold):
	FHW 	= open(out_file_name+"_" + str(penality) + "_" + str(diff_threshold) ,"w"  )
	I 		= None
	with open(merged_file) as FH:
		for line in FH:
			if "#" == line[0]:
				if I is not None:
					output(I, FHW, penality,diff_threshold)
				chrom,info 			= line[1:].strip("\n").split(":")
				start_stop, N,aN 	= info.split(",")
				start,stop 			= start_stop.split("-")
				I 					= mdt.segment(chrom,int(start),int(stop),float(N), annotation_N=int(aN))
			elif "~" == line[0]:
				I.insert_model_info(line)
			elif "N:"==line[:2] or "U:"==line[:2]:
				I.insert_component(line)
			# else:
			# 	line_array 				= line.strip("\n").split(",")
			# 	data_type,peak, data 	= line_array[0], line_array[1],",".join(line_array[2:])
			# 	if data_type != "dbSNP":
			# 		data 					= [(float(d.split(",")[0]),float(d.split(",")[1])) for d in data.split(":") ]
			# 	else:
			# 		data 					= [(float(d.split(",")[0]), d.split(",")) for d in data.split(":")  ]
			# 	setattr(I, data_type, data)
			# 	setattr(I, data_type+"_peak", bool(peak=="True"))
			# 	if not hasattr(I, "data_types"):
			# 		setattr(I, "data_types", list())
			# 	I.data_types.append(data_type)
	


if __name__ == "__main__":
	RUN 				= True
	if RUN:
		if len(sys.argv)==1:
			merged_file 	= "/Users/joeyazo/Desktop/Lab/gro_seq_files/HCT116/merged_data_file_100.txt"
			out 			= "/Users/joeyazo/Desktop/BIC_BEST"
			penality 		= 100
			diff_threshold 	= 10
		else:
			
			merged_file 	= sys.argv[1]
			out 			= sys.argv[2]
			penality 		= sys.argv[3]
			diff_threshold 	= sys.argv[4]
		
		run(merged_file, out, penality,diff_threshold)
	else:
		FILE 			= "/Users/joeyazo/Desktop/Lab/EMG_files/final_models_100_3.txt"
		read_in_display(FILE)

