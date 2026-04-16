import logging
import boto3
import tempfile
import os
from io import BytesIO
from typing import Generator, Dict, Any
from unstructured.partition.pdf import partition_pdf
from unstructured.partition.auto import partition
from langchain_community.document_loaders import UnstructuredPDFLoader
from langchain_core.documents import Document
from rag_pipeline.chunk import chunk_langchain_docs


class PDFProcessor:
    def __init__(self):
        # nikhil-todo: add support for other LLMs.
        # self.model = model
        pass

    def _load_pdf_from_local(self, file_path):
        logging.info(f"Loading PDF data from {file_path}")
        try:
            raw_pdf_elements = partition_pdf(
                filename=file_path,

                infer_table_structure=True,
                strategy="fast",

                extract_image_block_types=["Image"],
                extract_image_block_to_payload=True,

                chunking_strategy="by_title",
                mode='elements',
                max_characters=10000,
                new_after_n_chars=5000,
                combine_text_under_n_chars=2000,
                image_output_dir_path="data/",
            )

            # If no elements extracted, try OCR strategy
            if not raw_pdf_elements:
                logging.warning(
                    f"No text extracted with auto strategy. Attempting OCR "
                    f"strategy for {file_path}..."
                )
                raw_pdf_elements = partition_pdf(
                    filename=file_path,
                    strategy="ocr_only",
                    languages=["eng"],
                    extract_image_block_types=["Image"],
                    extract_image_block_to_payload=True,
                )
                if raw_pdf_elements:
                    logging.info(
                        f"Successfully extracted {len(raw_pdf_elements)} "
                        f"elements using OCR"
                    )

        except Exception as e:
            logging.error(f"Error processing PDF {file_path}: {e}", exc_info=True)
            raise

        logging.info(f"Pdf data finish loading, chunks now available!")
        return raw_pdf_elements

    # def _summarize_text_and_tables(self, text, tables):
    #     logging.info("Ready to summarize data with LLM")
    #     prompt_text = """You are an assistant tasked with summarizing text and tables. \

    #                     You are to give a concise summary of the table or text
    #                     and do nothing else.
    #                     Table or text chunk: {element} """
    #     prompt = ChatPromptTemplate.from_template(prompt_text)
    #     summarize_chain = (
    #         {"element": RunnablePassthrough()}
    #         | prompt | self.model | StrOutputParser()
    #     )
    #     logging.info(f"{self.model} done with summarization")
    #     return {
    #         "text": summarize_chain.batch(text, {"max_concurrency": 5}),
    #         "table": summarize_chain.batch(tables, {"max_concurrency": 5})
    #     }

    # nikhil-todo: check why S3 streaming for pdf loading is not working.
    # def _load_pdf_from_s3(self, file_path: str):

    #     logging.debug(f"Reading file from S3: {file_path}")
    #     try:
    #         # Create S3 client - works for public objects without credentials
    #         s3 = boto3.client('s3')
    #         path = file_path[5:]
    #         bucket, key = path.split('/', 1)
    #         s3_object = s3.get_object(Bucket=bucket, Key=key)
    #         s3_pdf_stream = s3_object['Body']

    #         # Read S3 stream into BytesIO to make it seekable
    #         # (S3 streams don't support seek operations required by
    #         # unstructured's file type detection)
    #         from io import BytesIO
    #         pdf_bytes = BytesIO(s3_pdf_stream.read())

    #         #nikhil-todo: check use of langchain_community.document_loaders.UnstructuredFileLoader
    #         # for lazy loading of the pdf file.
    #         try:
    #             raw_pdf_elements = partition(
    #                 file=pdf_bytes,
    #                 strategy="ocr_only",
    #                 content_type="application/pdf"
    #             )
    #         finally:
    #             # It's good practice to close the stream
    #             s3_pdf_stream.close()
    #             pdf_bytes.close()

    #         return raw_pdf_elements

    #     except Exception as e:
    #         logging.error(f"Failed to read file from S3: {e}")
    #         raise RuntimeError(f"Failed to read file from S3: {e}")

    def _load_pdf_from_s3(self, file_path: str):

        logging.debug(f"Reading file from S3: {file_path}")
        try:
            # Create S3 client - works for public objects without credentials
            s3 = boto3.client('s3')
            path = file_path[5:]
            bucket, key = path.split('/', 1)

            # Download PDF from S3 to a temporary location

            # Get the object from S3
            logging.info(f"Getting object from S3: {bucket}, {key}")
            s3_object = s3.get_object(Bucket=bucket, Key=key)
            s3_pdf_stream = s3_object['Body']
            pdf_bytes = BytesIO(s3_pdf_stream.read())
            s3_pdf_stream.close()

            # Create a temporary file to store the PDF
            temp_file_path = ""
            with tempfile.NamedTemporaryFile(suffix='.pdf', delete=False) as temp_file:
                temp_file.write(pdf_bytes.getvalue())
                temp_file_path = temp_file.name
                pdf_bytes.close()
            logging.info(f"Creating temporary file: {temp_file_path}")

            try:
                # nikhil-todo: check use of UnstructuredPDFLoader to load and partition the PDF
                # Use UnstructuredPDFLoader to load and partition the PDF
                # loader = UnstructuredPDFLoader(temp_file_path)
                # raw_pdf_elements = loader.load()
                raw_pdf_elements = partition_pdf(
                    filename=temp_file_path,

                    infer_table_structure=True,
                    strategy="auto",

                    extract_image_block_types=["Image"],
                    extract_image_block_to_payload=True,

                    chunking_strategy="by_title",
                    mode='elements',
                    max_characters=10000,
                    new_after_n_chars=5000,
                    combine_text_under_n_chars=2000,
                    image_output_dir_path="data/",
                )
                logging.info(
                    f"Successfully loaded {len(raw_pdf_elements)} "
                    f"elements from S3 PDF"
                )
                return raw_pdf_elements
            finally:
                # Clean up the temporary file
                if os.path.exists(temp_file_path):
                    os.remove(temp_file_path)

        except Exception as e:
            logging.error(f"Failed to read file from S3: {e}")
            raise RuntimeError(f"Failed to read file from S3: {e}")

    def process_pdf_data(
        self, file_path: str, chunk_args: Dict[str, Any] = {}
    ) -> Generator[str, None, None]:
        logging.debug(f"Processing PDF data from {file_path}")

        raw_pdf_elements = []
        if file_path.startswith("s3://"):
            raw_pdf_elements = self._load_pdf_from_s3(file_path)
        else:
            raw_pdf_elements = self._load_pdf_from_local(file_path)

        # nikhil-todo: add support for tables and text.
        # tables = [element.metadata.text_as_html for element in
        #             pdf_elements if 'Table' in str(type(element))]

        # text = [element.text for element in pdf_elements if
        #         'CompositeElement' in str(type(element))]

        # summaries = summarize_text_and_tables(text, tables)

        # Convert the raw PDF elements to LangChain documents
        langchain_docs_from_pdf = []
        for el in raw_pdf_elements:
            # Convert the element's metadata to a simple dict
            metadata = el.metadata.to_dict()
            # Create the LangChain Document
            doc = Document(page_content=str(el), metadata=metadata)
            langchain_docs_from_pdf.append(doc)

        logging.info(
            f"LangChain documents created from Raw PDF Elements: "
            f"{len(langchain_docs_from_pdf)} documents created"
        )

        # nikhil-todo: chunk args should be configured from the request chunk_embedding_kwargs.
        # Use default splitter and args if not provided
        splitter = chunk_args.get('splitter', 'recursive_character')
        args = chunk_args.get(
            'args', '{"chunk_size": 1000, "chunk_overlap": 100}'
        )
        logging.debug(
            f"chunking paragraph: with splitter: {splitter} and args: {args}"
        )
        chunked_docs = chunk_langchain_docs(splitter, langchain_docs_from_pdf, args)
        for chunked_text in chunked_docs:
            yield chunked_text
