default:
	@echo "Choose one of the Makefiles:"
	@ls Makefile.* | xargs -n 1 echo "make -f"
clean:
	rm -rf bin
	ls Makefile.* | xargs -n 1 make clean -f
