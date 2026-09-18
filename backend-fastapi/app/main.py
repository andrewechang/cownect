from fastapi import FastAPI

app = FastAPI(
    title="Cownect Backend API",
    version="0.1.0",
)


@app.get("/")
def root():
    return {"message": "Cownect Backend API is running"}


@app.get("/health")
def health():
    return {"status": "healthy"}