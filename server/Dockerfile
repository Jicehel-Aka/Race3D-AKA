FROM node:22-alpine
WORKDIR /app
COPY package.json package-lock.json ./
RUN npm ci --omit=dev
COPY server.ts ./
COPY shared ./shared
ENV PORT=10000
EXPOSE 10000
CMD ["npx", "tsx", "server.ts"]
